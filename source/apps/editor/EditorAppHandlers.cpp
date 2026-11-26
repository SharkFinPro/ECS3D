#include "EditorApp.h"
#include <ProjectPacker.h>
#include <Replication.h>
#include <assets/AssetRegistry.h>
#include <edits/RecordEdits.h>
#include <scenes/SceneManager.h>
#include <scenes/SceneAsset.h>
#include <objects/ObjectManager.h>
#include <SaveUI.h>
#include <Selection.h>
#include <TerrainCreateFlow.h>
#include <UnsavedChanges.h>
#include <objects/components/Component.h>
#include <terrain/TerrainAddition.h>
#include <terrain/TerrainFiles.h>
#include <terrain/TerrainStore.h>
#include <NetClient.h>
#include <Log.h>
#include <AssetDragDrop.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>
#include <uuid.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
  uuids::uuid randomUuid()
  {
    std::mt19937 rng{ std::random_device{}() };
    uuids::uuid_random_generator generator{ rng };
    return generator();
  }

  uuids::uuid makeProjectLoadId()
  {
    return randomUuid();
  }

  // How long a placed terrain object is waited for before the selection is given up.
  constexpr auto placedTerrainWait = std::chrono::seconds(10);

  // Nothing faithful could be derived for an edit (see RecordEdits.h): the edit still goes out, only the
  // history entry is skipped.
  void logUnrecordedEdit(const std::string& what)
  {
    Log::debug(LogCategory::editor, "Not recording " + what + " in the edit history.");
  }
}

// Register a new asset: apply locally for instant feedback, then on the server (which re-snapshots).
// Shared by the asset browser's import/create and the object tree's "Save as Prefab".
void EditorApp::onAddAsset(const nlohmann::json& asset)
{
  static_cast<void>(addAsset(asset, false));
}

bool EditorApp::addAsset(const nlohmann::json& asset, const bool requireApplied)
{
  // Derived before the local apply: the command's before state is the record this registry is about
  // to lose. Same for the two ops below.
  auto command = edits::commandForAddAsset(asset, *m_assetRegistry);

  const bool applied = replication::applyAddAssetWithTerrain(*m_assetRegistry, *m_sceneManager, m_componentRegistry,
                                                             *m_terrainStore, asset, false);
  if (requireApplied && !applied)
  {
    return false;
  }

  m_netClient->send(replication::packAddAsset(asset));

  if (command)
  {
    m_playHistory.current().record(std::move(*command));
    noteRecordedEdit();
  }
  else
  {
    m_saveUI->markEdited();
    logUnrecordedEdit("an addAsset");
  }

  return true;
}

// The terrain asset record goes first, which makes our own store hold an empty volume that takes the generated chunks;
// the same chunks go up to the server, which waits for them before it lets the scene play. The object that shows
// the terrain is an ordinary scene edit, so it is recorded for undo like any other added object. The asset record is
// not undoable, so undoing the object leaves the terrain in the Assets panel.
void EditorApp::onCreateTerrain(GeneratedTerrain&& generated)
{
  const auto scene = m_sceneManager->getCurrentScene();
  if (!scene)
  {
    throw std::runtime_error("There is no active scene to place the terrain in.");
  }

  auto asset = terrain::addTerrainAssetJson(generated.description);
  asset["displayName"] = generated.displayName;

  if (!terrain::parseAddedTerrain(asset) || m_terrainStore->find(generated.description.uuid) != nullptr)
  {
    throw std::runtime_error("The terrain could not be created.");
  }

  if (!addAsset(asset, true))
  {
    throw std::runtime_error("The editor did not accept the new terrain.");
  }

  // Past this point the terrain is created and sent, so a failure here must not reopen the form for a second create.
  queueCreatedTerrainUpload(generated);

  const auto objectUUID = randomUuid();

  try
  {
    onSceneEdit(terrainCreate::placementEdit(objectUUID, generated.displayName, generated.description.uuid));
  }
  catch (const std::exception& e)
  {
    Log::error(LogCategory::editor, std::string("Placing the new terrain failed to record: ") + e.what());
  }

  m_placedTerrainObject = objectUUID;
  m_selectionWhenPlaced.kind = m_selection->kind();
  m_selectionWhenPlaced.items.assign(m_selection->items().begin(), m_selection->items().end());
  m_placedTerrainDeadline = std::chrono::steady_clock::now() + placedTerrainWait;

  Log::info(LogCategory::editor, "Created terrain " + generated.displayName + " (" +
                                   std::to_string(generated.description.chunkCount) + " chunk(s)); uploading it.");
}

void EditorApp::queueCreatedTerrainUpload(const GeneratedTerrain& generated)
{
  try
  {
    const auto volume = m_terrainStore->find(generated.description.uuid);
    if (volume)
    {
      volume->restoreChunks(generated.volume->snapshotChunks());
    }
    else
    {
      Log::error(LogCategory::editor, "The editor's terrain store lost the new terrain right after creating it.");
    }

    if (m_terrainUpload.done())
    {
      m_terrainUploadSent = 0;
    }

    m_terrainUpload.add(generated.description.uuid, generated.volume);
    m_createdTerrains[generated.description.uuid] = std::chrono::steady_clock::now();
  }
  catch (const std::exception& e)
  {
    Log::error(LogCategory::editor, std::string("The new terrain's chunks could not be queued for upload: ") +
                                      e.what());
  }
}

void EditorApp::selectPlacedTerrain()
{
  if (!m_placedTerrainObject)
  {
    return;
  }

  // A click on something else in the meantime wins over the late arrival.
  if (m_selection->kind() != m_selectionWhenPlaced.kind ||
      !std::ranges::equal(m_selection->items(), m_selectionWhenPlaced.items))
  {
    m_placedTerrainObject.reset();
    return;
  }

  const auto scene = m_sceneManager->getCurrentScene();
  if (scene && scene->getObjectManager()->getObjectByUUID(*m_placedTerrainObject))
  {
    m_selection->selectObject(*m_placedTerrainObject);
    m_placedTerrainObject.reset();
  }
  else if (std::chrono::steady_clock::now() > m_placedTerrainDeadline)
  {
    m_placedTerrainObject.reset();
  }
}

// Rename an asset (a display-name override, or a scene's real name). Same local-apply-then-send shape as
// onAddAsset, except a refused rename is neither sent nor recorded.
void EditorApp::onRenameAsset(const uuids::uuid& assetUUID, const std::string& displayName)
{
  const auto op = replication::buildRenameAsset(assetUUID, displayName);
  auto command = edits::commandForRenameAsset(op, *m_assetRegistry);

  if (!replication::applyRenameAsset(*m_assetRegistry, *m_sceneManager, op))
  {
    logMessage("Info", "Refused the asset rename: that name is not available.");
    return;
  }

  m_netClient->send(replication::packRenameAsset(op));

  if (command)
  {
    m_playHistory.current().record(std::move(*command));
    noteRecordedEdit();
  }
  else
  {
    m_saveUI->markEdited();
    logUnrecordedEdit("a renameAsset");
  }
}

// Delete an asset: same local-apply-then-send shape. References dangle (lookups null-tolerate a missing
// uuid). A scene the project cannot spare (the active or only one) is refused locally, so nothing is sent.
void EditorApp::onRemoveAsset(const uuids::uuid& assetUUID)
{
  const auto op = replication::buildRemoveAsset(assetUUID);

  // The editor's SceneManager does not track the run status, so the play gate is checked here; the server
  // enforces it through SceneManager::removeScene.
  const auto* record = m_assetRegistry->getByUUID(assetUUID);
  if (record && record->type == AssetType::Scene && m_sceneStatus != SceneStatus::stopped)
  {
    logMessage("Info", "Can't delete a scene while the scene is running or paused; stop it first.");
    return;
  }

  auto command = edits::commandForRemoveAsset(op, *m_assetRegistry, *m_sceneManager);

  if (!replication::applyRemoveAsset(*m_assetRegistry, *m_sceneManager, op))
  {
    logMessage("Info", "Can't delete that asset: it is unknown, or it is the active or only scene.");
    return;
  }

  m_netClient->send(replication::packRemoveAsset(op));

  if (command)
  {
    m_playHistory.current().record(std::move(*command));
    noteRecordedEdit();
  }
  else
  {
    m_saveUI->markEdited();
    logUnrecordedEdit("a removeAsset");
  }
}

// A component value edit: send the component's new state to the server. Only the Inspector fires this.
void EditorApp::onEditComponent(const uuids::uuid& objectUUID, const std::shared_ptr<Component>& component) const
{
  const auto message = replication::buildComponentEdit(objectUUID, component);
  m_netClient->send(message);

  // A value edit joins the history when it commits, so until then the project is only held dirty.
  if (m_playHistory.hasStash())
  {
    m_saveUI->markEdited();
  }
  else
  {
    m_saveUI->beginUncommittedEdit();
  }
}

// A structural change (add/remove/reparent object, add/remove component, rename, add script): the
// server applies it and re-snapshots. Shared by the object tree and the Inspector.
void EditorApp::onSceneEdit(const nlohmann::json& edit)
{
  const auto scene = m_sceneManager->getCurrentScene();
  auto commands = scene ? edits::commandsForSceneEdit(edit, *scene->getObjectManager(), m_assetRegistry.get(),
                                                    scene.get())
                       : std::optional<std::vector<edits::EditCommand>>{};

  const auto payload = edit.dump();

  net::Message message(net::MessageType::sceneEdit);
  for (const std::vector<uint8_t> chunks(payload.begin(), payload.end()); const auto& chunk : chunks)
  {
    message.write(chunk);
  }
  m_netClient->send(message);

  if (commands && !commands->empty())
  {
    try
    {
      m_playHistory.current().recordBatch(std::move(*commands));
    }
    catch (...)
    {
      m_saveUI->markEdited();
      throw;
    }

    noteRecordedEdit();
    return;
  }

  m_saveUI->markEdited();

  if (!commands && scene)
  {
    logUnrecordedEdit("the scene edit " + payload);
  }
}

// One finished value edit, as the component looked before the gesture and after it. The per-frame
// onEditComponent send above is what keeps the server current while it is still going.
void EditorApp::onEditCommitted(const uuids::uuid& objectUUID, const nlohmann::json& before, const nlohmann::json& after)
{
  if (before == after)
  {
    return;
  }

  m_playHistory.current().record(edits::EditCommand::componentEdit(objectUUID, before, after));
  noteRecordedEdit();
}

// A recorded edit is covered by the history position, except while a play session runs (its history is
// discarded on Stop) or the connection is down (the edit never reached the server, so undoing it proves
// nothing); those keep the project dirty the way an unrecorded edit does.
void EditorApp::noteRecordedEdit() const
{
  if (editNeedsLatch(m_playHistory.hasStash(), m_netClient->isConnected()))
  {
    m_saveUI->markEdited();
  }
}

namespace {
  // The server's fixed step, so an upload moves at the rate the server streams.
  constexpr auto terrainUploadPeriod = std::chrono::milliseconds(20);
}

// Switch the active scene: apply locally for instant feedback, then tell the server (which re-snapshots).
// Shared by the asset browser's scene double-click and the Inspector's "Load Scene" button.
void EditorApp::onLoadScene(const uuids::uuid& sceneUUID)
{
  if (const auto scene = m_sceneManager->getScene(sceneUUID))
  {
    // Every recorded command names objects in the scene being left behind.
    clearEditHistory();
    m_historyScope.reset(sceneUUID);

    m_sceneManager->loadScene(scene);
  }

  net::Message message(net::MessageType::sceneControl);
  message.write(net::SceneControlOp::loadScene);
  message.writeString(uuids::to_string(sceneUUID));
  m_netClient->send(message);
}

void EditorApp::pumpTerrainUpload()
{
  if (m_terrainUpload.done() || !m_netClient->isConnected())
  {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  if (now - m_lastTerrainUploadPump < terrainUploadPeriod)
  {
    return;
  }

  m_lastTerrainUploadPump = now;

  try
  {
    m_terrainUploadSent += m_terrainUpload.pump([this](const net::Message& message) { m_netClient->send(message); });
  }
  catch (const std::exception& e)
  {
    Log::error(LogCategory::editor, std::string("Terrain upload failed: ") + e.what());
    m_terrainUpload.clear();
    return;
  }

  if (m_terrainUpload.done())
  {
    Log::info(LogCategory::editor, "Uploaded " + std::to_string(m_terrainUploadSent) + " terrain chunk(s)");
  }
}

void EditorApp::acceptSceneDrop(const float x, const float y, const float width, const float height)
{
  // A read-only viewer follows the server's scene, the same gate as the asset browser's double-click.
  if (!m_shouldDisplayGui || !m_serverEditable || !ImGui::GetDragDropPayload())
  {
    return;
  }

  if (!ImGui::BeginDragDropTargetCustom(ImRect(x, y, x + width, y + height), ImGui::GetID("sceneViewDrop")))
  {
    return;
  }

  if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(assetDragDrop::scene))
  {
    if (const auto sceneUUID = assetDragDrop::sceneFromPayload(payload->Data, payload->DataSize, *m_assetRegistry))
    {
      // Reloading the active scene restarts it on the server, which is too costly for a stray drop.
      const auto current = m_sceneManager->getCurrentScene();
      if (!current || current->getUUID() != *sceneUUID)
      {
        onLoadScene(*sceneUUID);
      }
    }
    else
    {
      Log::debug(LogCategory::editor, "A scene drop onto the viewport did not name a registered scene.");
    }
  }

  ImGui::EndDragDropTarget();
}

// A prefab body edit: re-register under the existing name, updating the body in place and keeping the
// uuid - the same path "Save as Prefab" over an existing name takes, via the same onAddAsset shape.
void EditorApp::onUpdatePrefabBody(const uuids::uuid& assetUUID, const std::string& name, const std::string& body)
{
  onAddAsset({
    { "assetType", "prefab" },
    { "uuid", uuids::to_string(assetUUID) },
    { "name", name },
    { "body", body }
  });
}

void EditorApp::onLoadProject()
{
  // Open/New: the server owns the running sim, so send it the packed project (SaveUI already applied it
  // to our managers); it reloads and re-snapshots.
  clearEditHistory();
  const auto loadedScene = m_sceneManager->getCurrentScene();
  m_historyScope.reset(loadedScene ? std::optional(loadedScene->getUUID()) : std::nullopt);

  const auto loadId = makeProjectLoadId();
  m_projectPacker->setProjectLoadId(loadId);
  m_projectPacker->setProjectFingerprint(0);
  m_projectLoads.expectOwnLoad(loadId);

  // The server resets its terrain on loadProject and expects the chunk counts the packed descriptions carry, so the
  // project's terrain files load into our store before packing and go up afterward.
  m_terrainStore->reconcile(*m_assetRegistry);
  m_terrainStore->resetVolumes();

  if (!m_projectRoot.empty())
  {
    const auto report = terrain::loadProjectTerrains(m_projectRoot, *m_assetRegistry, *m_terrainStore);

    for (const auto& error : report.errors)
    {
      Log::warn(LogCategory::editor, "Terrain failed to load: " + error);
    }

    Log::info(LogCategory::editor, "Loaded " + std::to_string(report.loaded) + " terrain(s) for upload");
  }

  net::Message message(net::MessageType::loadProject);
  m_projectPacker->pack(message);

  Log::info(LogCategory::editor, "Sending loadProject (" + std::to_string(message.size()) + " bytes) to server.");

  m_netClient->send(message);

  // Holds the volumes themselves: our store is reset again when our own snapshot comes back.
  m_terrainUpload.clear();
  m_terrainUploadSent = 0;

  for (const auto& uuid : m_terrainStore->terrains())
  {
    if (const auto volume = m_terrainStore->find(uuid); volume && !volume->chunks().empty())
    {
      m_terrainUpload.add(uuid, volume);
    }
  }
}
