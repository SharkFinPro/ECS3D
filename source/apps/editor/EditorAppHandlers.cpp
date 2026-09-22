#include "EditorApp.h"
#include <ProjectPacker.h>
#include <Replication.h>
#include <assets/AssetRegistry.h>
#include <edits/RecordEdits.h>
#include <scenes/SceneManager.h>
#include <scenes/SceneAsset.h>
#include <objects/ObjectManager.h>
#include <SaveUI.h>
#include <objects/components/Component.h>
#include <NetClient.h>
#include <Log.h>
#include <objects/Object.h>
#include <nlohmann/json.hpp>
#include <uuid.h>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
  // Whether any of the object's components mention the asset uuid in their serialized form. Searching the
  // serialized form keeps this generic (no component type named here); only model/texture references -
  // the ones that would dangle - ever match.
  bool objectReferencesAsset(const std::shared_ptr<Object>& object, const std::string& uuidString)
  {
    for (const auto& [type, component] : object->getComponents())
    {
      if (component->serialize().dump().find(uuidString) != std::string::npos)
      {
        return true;
      }
    }

    return false;
  }

  // Count the object nodes within a serialized prefab body (one Object tree) that reference the uuid.
  int countReferencesInPrefabNode(const nlohmann::json& node, const std::string& uuidString)
  {
    int count = 0;

    if (const auto components = node.find("components"); components != node.end() && components->is_array())
    {
      for (const auto& component : *components)
      {
        if (component.dump().find(uuidString) != std::string::npos)
        {
          ++count;
          break;
        }
      }
    }

    if (const auto children = node.find("children"); children != node.end() && children->is_array())
    {
      for (const auto& child : *children)
      {
        if (child.is_object())
        {
          count += countReferencesInPrefabNode(child, uuidString);
        }
      }
    }

    return count;
  }

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
  // Derived before the local apply: the command's before state is the record this registry is about
  // to lose. Same for the two ops below.
  auto command = edits::commandForAddAsset(asset, *m_assetRegistry);

  replication::applyAddAsset(*m_assetRegistry, *m_sceneManager, m_componentRegistry, asset);

  m_netClient->send(replication::packAddAsset(asset));
  m_saveUI->markEdited();

  if (command)
  {
    m_editHistory.record(std::move(*command));
  }
  else
  {
    logUnrecordedEdit("an addAsset");
  }
}

// Rename an asset (display-name override only). Same local-apply-then-send shape as onAddAsset.
void EditorApp::onRenameAsset(const uuids::uuid& assetUUID, const std::string& displayName)
{
  const auto op = replication::buildRenameAsset(assetUUID, displayName);
  auto command = edits::commandForRenameAsset(op, *m_assetRegistry);

  replication::applyRenameAsset(*m_assetRegistry, op);

  m_netClient->send(replication::packRenameAsset(op));
  m_saveUI->markEdited();

  if (command)
  {
    m_editHistory.record(std::move(*command));
  }
  else
  {
    logUnrecordedEdit("a renameAsset");
  }
}

// Delete an asset: same local-apply-then-send shape. Deletion always succeeds; references dangle
// (lookups null-tolerate a missing uuid).
void EditorApp::onRemoveAsset(const uuids::uuid& assetUUID)
{
  const auto op = replication::buildRemoveAsset(assetUUID);
  auto command = edits::commandForRemoveAsset(op, *m_assetRegistry);

  replication::applyRemoveAsset(*m_assetRegistry, op);

  m_netClient->send(replication::packRemoveAsset(op));
  m_saveUI->markEdited();

  if (command)
  {
    m_editHistory.record(std::move(*command));
  }
  else
  {
    logUnrecordedEdit("a removeAsset");
  }
}

// How many objects reference an asset by uuid, for the delete-confirmation modal's warning. Scans the
// replicated scenes' objects and every prefab body - the two places an object tree lives editor-side.
int EditorApp::countAssetReferences(const uuids::uuid& assetUUID) const
{
  const auto uuidString = uuids::to_string(assetUUID);
  int count = 0;

  for (const auto& [sceneUUID, scene] : m_sceneManager->getScenes())
  {
    const auto objectManager = scene->getObjectManager();
    if (!objectManager)
    {
      continue;
    }

    for (const auto& object : objectManager->getAllObjects())
    {
      if (objectReferencesAsset(object, uuidString))
      {
        ++count;
      }
    }
  }

  for (const auto& [recordUUID, record] : m_assetRegistry->getAssets())
  {
    if (record.type != AssetType::Prefab)
    {
      continue;
    }

    if (auto body = nlohmann::json::parse(record.body, nullptr, false); !body.is_discarded() && body.is_object())
    {
      count += countReferencesInPrefabNode(body, uuidString);
    }
  }

  return count;
}

// A component value edit: send the component's new state to the server. Only the Inspector fires this.
void EditorApp::onEditComponent(const uuids::uuid& objectUUID, const std::shared_ptr<Component>& component) const
{
  const auto message = replication::buildComponentEdit(objectUUID, component);
  m_netClient->send(message);
  m_saveUI->markEdited();
}

// A structural change (add/remove/reparent object, add/remove component, rename, add script): the
// server applies it and re-snapshots. Shared by the object tree and the Inspector.
void EditorApp::onSceneEdit(const nlohmann::json& edit)
{
  const auto scene = m_sceneManager->getCurrentScene();
  auto command = scene ? edits::commandForSceneEdit(edit, *scene->getObjectManager())
                       : std::optional<edits::EditCommand>{};

  const auto payload = edit.dump();

  net::Message message(net::MessageType::sceneEdit);
  for (const std::vector<uint8_t> chunks(payload.begin(), payload.end()); const auto& chunk : chunks)
  {
    message.write(chunk);
  }
  m_netClient->send(message);
  m_saveUI->markEdited();

  if (command)
  {
    m_editHistory.record(std::move(*command));
  }
  else if (scene)
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

  m_editHistory.record(edits::EditCommand::componentEdit(objectUUID, before, after));
}

// Switch the active scene: apply locally for instant feedback, then tell the server (which re-snapshots).
// Shared by the asset browser's scene double-click and the Inspector's "Load Scene" button.
void EditorApp::onLoadScene(const uuids::uuid& sceneUUID)
{
  if (const auto scene = m_sceneManager->getScene(sceneUUID))
  {
    // Every recorded command names objects in the scene being left behind.
    m_editHistory.clear();

    m_sceneManager->loadScene(scene);
  }

  net::Message message(net::MessageType::sceneControl);
  message.write(net::SceneControlOp::loadScene);
  message.writeString(uuids::to_string(sceneUUID));
  m_netClient->send(message);
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
  m_editHistory.clear();

  net::Message message(net::MessageType::loadProject);
  m_projectPacker->pack(message);

  Log::info(LogCategory::editor, "Sending loadProject (" + std::to_string(message.size()) + " bytes) to server.");

  m_netClient->send(message);
}
