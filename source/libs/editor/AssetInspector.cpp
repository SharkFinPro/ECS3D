#include "AssetInspector.h"
#include "AssetDisplay.h"
#include "GuiComponents.h"
#include "ObjectInspector.h"
#include "TransientObject.h"
#include <GpuAssetCache.h>
#include <Replication.h>
#include <assets/AssetRegistry.h>
#include <objects/Object.h>
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/window/Window.h>
#include <VulkanEngine/components/assets/textures/Texture2D.h>
#include <VulkanEngine/components/assets/objects/Model.h>
#include <stb_image.h>
#include <nlohmann/json.hpp>
#include <imgui.h>
#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

AssetInspector::AssetInspector(std::shared_ptr<GpuAssetCache> assetCache,
                               std::shared_ptr<ComponentRegistry> componentRegistry,
                               std::shared_ptr<ComponentEditor> componentEditor)
  : m_assetCache(std::move(assetCache)),
    m_componentRegistry(std::move(componentRegistry)),
    m_prefabBody(std::make_unique<TransientObject>(m_componentRegistry)),
    m_prefabInspector(std::make_unique<ObjectInspector>(std::move(componentEditor)))
{
  // The reused inspector edits a detached object, so its edits must apply locally rather than travel to the
  // server. A value edit already mutated the component in place — just mark the body dirty. A structural
  // edit is queued and applied after display() returns (applying it now would mutate the component map
  // mid-iteration). Neither sends immediately; displayPrefabBody coalesces the send (see m_prefabBodyDirty).
  m_prefabInspector->setEditCallback([this](const uuids::uuid&, const std::shared_ptr<Component>&) {
    m_prefabBodyDirty = true;
  });
  m_prefabInspector->setSceneEditCallback([this](const nlohmann::json& edit) {
    m_pendingPrefabEdits.push_back(edit);
  });

  // No viewport highlight for a detached prefab body.
  m_prefabInspector->setShowHighlightToggle(false);
}

// Defined here (not defaulted in the header) so the inspector/body unique_ptrs see their complete types.
AssetInspector::~AssetInspector() = default;

void AssetInspector::setLoadSceneCallback(LoadSceneCallback callback)
{
  m_onLoadScene = std::move(callback);
}

void AssetInspector::setRenameCallback(RenameCallback callback)
{
  m_onRename = std::move(callback);
}

void AssetInspector::setRemoveCallback(RemoveCallback callback)
{
  m_onRemove = std::move(callback);
}

void AssetInspector::setReferenceCountCallback(ReferenceCountCallback callback)
{
  m_onReferenceCount = std::move(callback);
}

void AssetInspector::setUpdatePrefabBodyCallback(UpdatePrefabBodyCallback callback)
{
  m_onUpdatePrefabBody = std::move(callback);
}

void AssetInspector::setAssetRegistry(const AssetRegistry* registry)
{
  m_prefabInspector->setAssetRegistry(registry);
}

namespace {
  // A display-name rename is a display-only override the AssetRegistry packs (see AssetRegistry::pack): it
  // survives a snapshot only for the flat file assets. A Scene record is regenerated from the SceneManager
  // on every snapshot (and never packed), so its override wouldn't stick — keep the scene name read-only.
  bool isRenamable(const AssetType type)
  {
    return type == AssetType::Model || type == AssetType::Texture
        || type == AssetType::Script || type == AssetType::Prefab;
  }
}

void AssetInspector::setEditable(const bool editable)
{
  m_editable = editable;
  m_prefabInspector->setEditable(editable);
}

void AssetInspector::displayTypeChip(const AssetRecord& record) const
{
  const char* label = assetDisplay::typeLabel(record.type);
  ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - gc::iconPillWidth(label));
  gc::iconPill(assetDisplay::icon(record.type), label, assetDisplay::color(record.type));
}

void AssetInspector::display(const AssetRecord& record, const std::optional<uuids::uuid>& activeSceneUUID)
{
  // Recompute the file metadata only when the selection changes, not every frame.
  if (!m_metaLoaded || m_metaUUID != record.uuid)
  {
    m_metaUUID = record.uuid;
    m_metaLoaded = true;
    refreshMeta(record);
  }

  displayHeader(record);

  switch (record.type)
  {
    case AssetType::Texture: displayTextureBody(record); break;
    case AssetType::Model:   displayModelBody(record);   break;
    case AssetType::Script:  displayScriptBody();         break;
    case AssetType::Scene:   displaySceneBody(record, activeSceneUUID); break;
    case AssetType::Prefab:  displayPrefabBody(record);   break;
    default: break;
  }

  // Delete lives at the foot of the panel, for the flat file assets only (a Scene's removal isn't a
  // registry op — see isRenamable). The modal is drawn every frame the button is armed.
  displayDeleteButton(record);
  displayDeleteConfirmationModal(record);
}

void AssetInspector::displayHeader(const AssetRecord& record)
{
  // Re-seed the name buffer with the effective display name when the selection changes (only then, so an
  // external rename can't overwrite an in-progress edit — same discipline as ObjectInspector).
  if (m_nameEditUUID != record.uuid)
  {
    m_nameEditUUID = record.uuid;
    const auto name = assetDisplay::name(record);
    const auto len = std::min(name.size(), m_nameEditBuffer.size() - 1);
    name.copy(m_nameEditBuffer.data(), len);
    m_nameEditBuffer[len] = '\0';
  }

  // Display name: an editable rename field for the flat file assets (gated on editable), read-only text
  // for scenes (their override wouldn't survive a snapshot — see isRenamable).
  if (isRenamable(record.type))
  {
    ImGui::BeginDisabled(!m_editable);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::InputText(("##assetName" + uuids::to_string(record.uuid)).c_str(),
                         m_nameEditBuffer.data(), m_nameEditBuffer.size()) && m_onRename)
    {
      // Immediate send on each edit (like object rename), so switching selection can't lose the change.
      m_onRename(record.uuid, m_nameEditBuffer.data());
    }
    ImGui::EndDisabled();
  }
  else
  {
    ImGui::TextColored(theme::t1, "%s", assetDisplay::name(record).c_str());
  }

  ImGui::Spacing();

  gc::rowLabel("UUID");
  ImGui::TextColored(theme::t3, "%s", uuids::to_string(record.uuid).c_str());

  // Scenes and prefabs have no file on disk (their `path` holds the display name), so only the file-backed
  // types show a source path.
  if (record.type != AssetType::Scene && record.type != AssetType::Prefab && !record.path.empty())
  {
    gc::rowLabel("Path");
    ImGui::TextColored(theme::t2, "%s", record.path.c_str());
  }
}

void AssetInspector::refreshMeta(const AssetRecord& record)
{
  m_fileSize.clear();
  m_haveImageSize = false;
  m_imageWidth = 0;
  m_imageHeight = 0;
  m_haveMeshStats = false;
  m_vertexCount = 0;
  m_indexCount = 0;
  m_haveScriptSource = false;
  m_scriptSource.clear();

  // Only file-backed types have a path on disk; scenes carry their name there, prefabs their inline body.
  if (record.type == AssetType::Scene || record.type == AssetType::Prefab || record.path.empty())
  {
    return;
  }

  if (const auto size = assetDisplay::fileSize(record.path))
  {
    m_fileSize = *size;
  }

  // The GPU texture doesn't expose its extent, so read the dimensions straight from the file header
  // (stbi_info decodes no pixels). Degrades gracefully when the file isn't reachable editor-side.
  if (record.type == AssetType::Texture)
  {
    int comp = 0;
    m_haveImageSize = stbi_info(record.path.c_str(), &m_imageWidth, &m_imageHeight, &comp) != 0;
  }

  // Mesh stats come from the loaded vke::Model (its vertex/index arrays are already in memory once the
  // model is cached). Resolving it here — once per selection — reuses the same cache the renderer uses.
  if (record.type == AssetType::Model)
  {
    try
    {
      if (const auto model = m_assetCache->getModel(record.uuid))
      {
        m_vertexCount = model->getVertices().size();
        m_indexCount = model->getIndices().size();
        m_haveMeshStats = true;
      }
    }
    catch (const std::exception&)
    {
      // The model file may be unreachable editor-side; leave the stats unshown.
    }
  }

  // Read the script source from disk (the editor side has the .cs file). Degrades to "not available"
  // when the file isn't reachable — e.g. attached to a server that shares no filesystem.
  if (record.type == AssetType::Script)
  {
    if (std::ifstream in(record.path, std::ios::binary); in)
    {
      std::ostringstream ss;
      ss << in.rdbuf();
      m_scriptSource = ss.str();
      m_haveScriptSource = true;

      // Guard against a pathologically large file dragging the panel down.
      constexpr size_t maxBytes = 256 * 1024;
      if (m_scriptSource.size() > maxBytes)
      {
        m_scriptSource.resize(maxBytes);
        m_scriptSource += "\n\n... (truncated)";
      }
    }
  }
}

void AssetInspector::displayTextureBody(const AssetRecord& record)
{
  ImGui::Spacing();
  gc::sectionLabel("Preview");
  ImGui::Spacing();

  // Same fallback-to-icon behavior as the browser tiles: use the cached GPU texture when it resolves,
  // otherwise fall through to the type icon.
  ImTextureID thumb = 0;
  try
  {
    if (const auto texture = m_assetCache->getTexture(record.uuid))
    {
      thumb = texture->getImGuiTexture();
    }
  }
  catch (const std::exception&)
  {
    // fall back to the type icon below
  }

  // Fit the preview inside the panel width and a scaled max height, preserving the image aspect ratio
  // when the dimensions are known (square otherwise).
  const float scale = m_assetCache->getRenderer()->getWindow()->getContentScale();
  const float maxW = ImGui::GetContentRegionAvail().x;
  const float maxH = 260.0f * scale;

  float boxW = maxW;
  float boxH = maxW;
  if (m_haveImageSize && m_imageWidth > 0 && m_imageHeight > 0)
  {
    const float aspect = static_cast<float>(m_imageWidth) / static_cast<float>(m_imageHeight);
    boxH = boxW / aspect;
    if (boxH > maxH)
    {
      boxH = maxH;
      boxW = boxH * aspect;
    }
  }

  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const ImVec2 p1(p0.x + boxW, p0.y + boxH);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p0, p1, theme::u32(theme::inset), 8.0f);
  gc::drawThumb(dl, p0, p1, thumb, assetDisplay::icon(record.type), assetDisplay::color(record.type), 8.0f);
  dl->AddRect(p0, p1, theme::u32(theme::line), 8.0f);
  ImGui::Dummy(ImVec2(boxW, boxH));

  ImGui::Spacing();

  if (m_haveImageSize)
  {
    gc::rowLabel("Dimensions");
    ImGui::TextColored(theme::t2, "%d x %d", m_imageWidth, m_imageHeight);
  }

  if (!m_fileSize.empty())
  {
    gc::rowLabel("Size");
    ImGui::TextColored(theme::t2, "%s", m_fileSize.c_str());
  }
}

void AssetInspector::displayModelBody(const AssetRecord& record)
{
  ImGui::Spacing();
  gc::sectionLabel("Preview");
  ImGui::Spacing();

  // A live 3D preview needs an offscreen render target the engine doesn't expose yet, so stand in the
  // type icon.
  const float scale = m_assetCache->getRenderer()->getWindow()->getContentScale();
  const float boxW = ImGui::GetContentRegionAvail().x;
  const float boxH = std::min(boxW, 200.0f * scale);
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const ImVec2 p1(p0.x + boxW, p0.y + boxH);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p0, p1, theme::u32(theme::inset), 8.0f);
  gc::drawThumb(dl, p0, p1, 0, assetDisplay::icon(record.type), assetDisplay::color(record.type), 8.0f);
  dl->AddRect(p0, p1, theme::u32(theme::line), 8.0f);
  ImGui::Dummy(ImVec2(boxW, boxH));

  ImGui::Spacing();

  std::string ext = std::filesystem::path(record.path).extension().string();
  if (!ext.empty() && ext.front() == '.')
  {
    ext.erase(0, 1); // drop the leading dot for the "Format" row
  }
  gc::rowLabel("Format");
  ImGui::TextColored(theme::t2, "%s", ext.empty() ? "-" : ext.c_str());

  if (!m_fileSize.empty())
  {
    gc::rowLabel("Size");
    ImGui::TextColored(theme::t2, "%s", m_fileSize.c_str());
  }

  if (m_haveMeshStats)
  {
    gc::rowLabel("Vertices");
    ImGui::TextColored(theme::t2, "%zu", m_vertexCount);

    gc::rowLabel("Triangles");
    ImGui::TextColored(theme::t2, "%zu", m_indexCount / 3);
  }
}

void AssetInspector::displayScriptBody()
{
  ImGui::Spacing();
  gc::sectionLabel("Source");
  ImGui::Spacing();

  if (!m_haveScriptSource)
  {
    gc::dashedBox("Source not available");
    return;
  }

  // A read-only multiline box: selectable/copyable and scrollable, filling the rest of the panel. The
  // buffer is the cached source (null-terminated via data()); ReadOnly keeps ImGui from writing to it.
  const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
  ImGui::InputTextMultiline("##scriptSource", m_scriptSource.data(), m_scriptSource.size() + 1,
                            size, ImGuiInputTextFlags_ReadOnly);
}

void AssetInspector::displaySceneBody(const AssetRecord& record, const std::optional<uuids::uuid>& activeSceneUUID)
{
  ImGui::Spacing();

  const bool isActive = activeSceneUUID.has_value() && activeSceneUUID.value() == record.uuid;

  // Status row: a green dot + "Active" when this is the loaded scene, else a muted "Inactive".
  gc::rowLabel("Status");
  {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float cy = p.y + ImGui::GetFrameHeight() * 0.5f;
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 5.0f, cy), 4.0f,
                                                theme::u32(isActive ? theme::sceneGreen : theme::t3));
    ImGui::SetCursorScreenPos(ImVec2(p.x + 16.0f, p.y));
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(isActive ? theme::sceneGreen : theme::t3, isActive ? "Active" : "Inactive");
  }

  ImGui::Spacing();

  // Load Scene: the same server-side active-scene switch as the browser's double-click, so gated on
  // editable. Also disabled when this scene is already active (a reload would be a no-op round-trip).
  ImGui::BeginDisabled(!m_editable || isActive);
  if (gc::accentButton("Load Scene", gc::SecIcon::scene) && m_onLoadScene)
  {
    m_onLoadScene(record.uuid);
  }
  ImGui::EndDisabled();
}

void AssetInspector::displayPrefabBody(const AssetRecord& record)
{
  ImGui::Spacing();
  gc::sectionLabel("Contents");
  ImGui::Spacing();

  // Switching to a different prefab with an edit still pending: flush it against the asset it belongs to
  // (m_prefabRecord*, still the old one) before syncFromBody rebuilds to the new body below.
  if (m_prefabBodyDirty && record.uuid != m_prefabRecordUUID)
  {
    flushPrefabBody();
  }

  // (Re)build the detached object when the body changed under us (a different prefab selected, or an
  // external snapshot). A flushed edit markSyncs the sent body, so its own echo won't rebuild.
  if (m_prefabBody->syncFromBody(record.body))
  {
    // The pending edit (if any) belongs to whatever we just loaded.
    m_prefabRecordUUID = record.uuid;
    m_prefabRecordName = record.path;
  }

  const auto& object = m_prefabBody->object();
  if (!object)
  {
    // Empty/malformed body (an older or hand-edited record) — nothing to deserialize.
    gc::dashedBox("Prefab body unavailable");
    return;
  }

  // Draw the body with the same component editors an object uses. The wired callbacks collect structural
  // edits and mark value edits dirty rather than sending them; apply the structural ones once display has
  // returned so they don't mutate the component map ObjectInspector::display is iterating.
  m_pendingPrefabEdits.clear();

  m_prefabInspector->display(object);

  if (auto* manager = m_prefabBody->manager())
  {
    for (const auto& edit : m_pendingPrefabEdits)
    {
      replication::applySceneEdit(*manager, edit);
      m_prefabBodyDirty = true;
    }
  }
  m_pendingPrefabEdits.clear();

  // Coalesce the send: a prefab body update makes the server re-snapshot the whole project, so hold it
  // until the user isn't actively dragging/typing a widget. The detached object already reflects the edit,
  // so local feedback stays instant; the network sees one update per interaction instead of per frame.
  if (m_prefabBodyDirty && !ImGui::IsAnyItemActive())
  {
    flushPrefabBody();
  }
}

void AssetInspector::flushPrefabBody()
{
  if (!m_prefabBodyDirty)
  {
    return;
  }

  m_prefabBodyDirty = false;

  if (!m_onUpdatePrefabBody || !m_prefabBody->object())
  {
    return;
  }

  // Re-register under the prefab's name (m_prefabRecordName): re-registering an existing name updates the
  // body in place and keeps the uuid — the same path "Save as Prefab" over an existing name takes.
  // markSynced so the resulting registry update (and the server's snapshot echo) doesn't rebuild the object.
  const auto newBody = m_prefabBody->serialize();
  m_onUpdatePrefabBody(m_prefabRecordUUID, m_prefabRecordName, newBody);
  m_prefabBody->markSynced(newBody);
}

void AssetInspector::displayDeleteButton(const AssetRecord& record)
{
  // Only the flat file assets can be deleted from here (a Scene lives in the SceneManager, not a registry
  // record we can drop — see isRenamable).
  if (!isRenamable(record.type))
  {
    return;
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  ImGui::BeginDisabled(!m_editable);

  // Danger-red full-width button (same palette as the modal's confirm).
  ImGui::PushStyleColor(ImGuiCol_Button, theme::danger);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::v4(240, 110, 114));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::v4(210, 70, 75));
  ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(255, 255, 255));
  if (ImGui::Button("Delete Asset", ImVec2(ImGui::GetContentRegionAvail().x, 34.0f)))
  {
    // Arm the modal and compute the reference count once, now, rather than every frame.
    m_assetPendingDeletion = record.uuid;
    m_pendingRefCount = m_onReferenceCount ? m_onReferenceCount(record.uuid) : 0;
  }
  ImGui::PopStyleColor(4);

  ImGui::EndDisabled();
}

void AssetInspector::displayDeleteConfirmationModal(const AssetRecord& record)
{
  // Armed only for the currently-shown asset. If the selection changed out from under an armed prompt
  // (the modal is normally blocking, but be defensive), drop it rather than confirm a stale delete.
  if (m_assetPendingDeletion != record.uuid)
  {
    m_assetPendingDeletion.reset();
    return;
  }

  ImGui::OpenPopup("Delete Asset?");

  bool shouldDelete = false;

  if (ImGui::BeginPopupModal("Delete Asset?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::TextUnformatted("Are you sure you want to delete");
    ImGui::SameLine();
    ImGui::TextColored(theme::accent, "%s", assetDisplay::name(record).c_str());
    ImGui::SameLine();
    ImGui::TextUnformatted("?");

    // Deletion always succeeds; any references are left to dangle (the slots show "None") — warn how many.
    if (m_pendingRefCount > 0)
    {
      ImGui::TextColored(theme::scriptAmber, "Referenced by %d object%s - those references will be left empty.",
                         m_pendingRefCount, m_pendingRefCount == 1 ? "" : "s");
    }

    ImGui::TextColored(theme::t3, "This action cannot be undone.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Danger-red confirm; neutral cancel (mirrors ObjectGUIManager's delete modal).
    ImGui::PushStyleColor(ImGuiCol_Button, theme::danger);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::v4(240, 110, 114));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::v4(210, 70, 75));
    ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(255, 255, 255));
    if (ImGui::Button("Delete", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter))
    {
      shouldDelete = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor(4);

    ImGui::SameLine();

    if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
      m_assetPendingDeletion.reset();
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }

  if (shouldDelete)
  {
    if (m_onRemove)
    {
      m_onRemove(record.uuid);
    }

    // The selection is cleared by InspectorPanel next frame: the local apply drops the uuid from the
    // registry, and the panel's stale-asset check clears the slot (the same path a fresh snapshot uses).
    m_assetPendingDeletion.reset();
  }
}
