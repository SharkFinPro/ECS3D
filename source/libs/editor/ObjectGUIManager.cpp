#include "ObjectGUIManager.h"
#include "AssetDragDrop.h"
#include "GuiComponents.h"
#include "Selection.h"
#include <Replication.h>
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <nlohmann/json.hpp>
#include <imgui.h>
#include <random>
#include <string>
#include <utility>

namespace {
  // A fresh asset uuid for a saved prefab. (AssetBrowserPanel has the same one-liner for the assets it
  // creates; asset uuids are unrelated to the scene's object uuids, so ObjectManager's generator is not
  // the right source here.)
  [[nodiscard]] std::string newAssetUUID()
  {
    std::mt19937 rng{ std::random_device{}() };
    uuids::uuid_random_generator generator{ rng };
    return uuids::to_string(generator());
  }

  // Heuristic icon for an object derived from its components (the mockup shows a per-object glyph). The
  // Inspector's ObjectInspector keeps its own copy of this mapping for the selected-object header chip.
  gc::SecIcon iconForObject(const std::shared_ptr<Object>& object)
  {
    const auto& components = object->getComponents();

    if (components.contains(ComponentType::lightRenderer))
    {
      return gc::SecIcon::light;
    }

    if (const auto collider = components.find(ComponentType::collider); collider != components.end())
    {
      if (collider->second->getSubType() == ComponentType::SubComponentType_sphereCollider)
      {
        return gc::SecIcon::sphere;
      }
      return components.contains(ComponentType::rigidBody) ? gc::SecIcon::rigidblock : gc::SecIcon::block;
    }

    if (components.contains(ComponentType::modelRenderer))
    {
      return gc::SecIcon::model;
    }

    return gc::SecIcon::block;
  }
}

void ObjectGUIManager::setSceneEditCallback(SceneEditCallback callback)
{
  m_sceneEditCallback = std::move(callback);
}

void ObjectGUIManager::setAddAssetCallback(AddAssetCallback callback)
{
  m_addAssetCallback = std::move(callback);
}

void ObjectGUIManager::setSelection(std::shared_ptr<EditorSelection> selection)
{
  m_selection = std::move(selection);
}

void ObjectGUIManager::setEditable(const bool editable)
{
  m_editable = editable;
}

void ObjectGUIManager::displayGui(const ObjectManager* objectManager)
{
  ImGui::Begin("Objects");

  if (!m_editable)
  {
    ImGui::TextColored(theme::scriptAmber, "Read-only - server is not in edit mode");
    ImGui::Separator();
  }

  // Section header with a right-aligned count pill (mockup: "OBJECTS  [10]").
  if (objectManager)
  {
    gc::sectionLabel("Objects");

    const auto count = std::to_string(objectManager->getObjects().size());
    const float pillWidth = ImGui::CalcTextSize(count.c_str()).x + 18.0f;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - pillWidth);
    gc::pill(count.c_str(), theme::t3);

    ImGui::Spacing();
  }

  ImGui::BeginDisabled(!m_editable || objectManager == nullptr);
  if (gc::accentButton("Create New Object", gc::SecIcon::plus))
  {
    m_sceneEditCallback(replication::buildAddObject("Object"));
  }
  ImGui::EndDisabled();

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  if (objectManager)
  {
    for (const auto& object : objectManager->getObjects())
    {
      displayObjectTree(object);
    }

    // The empty area below the tree is the scene root: drop an object there to reparent it to the root,
    // or a prefab from the asset browser to instantiate it.
    ImGui::Dummy(ImGui::GetContentRegionAvail());
    if (m_editable && ImGui::BeginDragDropTarget())
    {
      if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("object"))
      {
        const std::string uuidStr(static_cast<const char*>(payload->Data), payload->DataSize);
        if (const auto dragged = uuids::uuid::from_string(uuidStr); dragged.has_value() && m_sceneEditCallback)
        {
          m_sceneEditCallback(replication::buildReparentObject(dragged.value(), nullptr));
        }
      }

      if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(assetDragDrop::prefab))
      {
        const std::string uuidStr(static_cast<const char*>(payload->Data), payload->DataSize);
        if (const auto prefab = uuids::uuid::from_string(uuidStr); prefab.has_value() && m_sceneEditCallback)
        {
          // The server resolves the prefab uuid to its body on disk, instantiates, and re-snapshots.
          m_sceneEditCallback(replication::buildInstantiatePrefab(prefab.value()));
        }
      }

      ImGui::EndDragDropTarget();
    }
  }

  // Delete hotkey: while the Objects panel has focus, Delete queues the selected object for removal
  // (guarded against firing while a text field is being typed into, or when read-only). The
  // confirmation modal follows.
  if (m_editable && objectManager && m_selection->objectUUID().has_value() &&
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      !ImGui::GetIO().WantTextInput &&
      ImGui::IsKeyPressed(ImGuiKey_Delete))
  {
    m_objectPendingDeletion = m_selection->objectUUID();
  }

  ImGui::End();

  displayDeleteConfirmationModal(objectManager);
}

void ObjectGUIManager::displayObjectTree(const std::shared_ptr<Object>& object)
{
  ImGui::PushID(uuids::to_string(object->getUUID()).c_str());

  const bool isSelected = m_selection->objectUUID() == object->getUUID();
  const bool isLeaf = object->getChildren().empty();

  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_FramePadding |
                             ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap |
                             ImGuiTreeNodeFlags_OpenOnDoubleClick;
  if (isSelected)
  {
    flags |= ImGuiTreeNodeFlags_Selected;
  }
  if (isLeaf)
  {
    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
  }

  // Empty label: we draw the icon + name ourselves so the row matches the mockup (per-type glyph + an
  // accent left-bar on the selected row). Accent-tint the node's selection/hover fills.
  ImGui::PushStyleColor(ImGuiCol_Header, theme::accdim);
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, theme::hover);
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, theme::accdim);
  // Taller rows than the default frame padding so the row + its inline buttons have more breathing room.
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 11.0f));
  const bool open = ImGui::TreeNodeEx("##node", flags);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(3);

  if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
  {
    // The Inspector's ObjectInspector folds its Add Component list closed on its own when it notices the
    // selection changed, so the tree only needs to update the shared selection here.
    m_selection->selectObject(object->getUUID());
  }

  // Icon + name + accent selection bar, drawn over the (empty-label) node row.
  const float rowHeight = ImGui::GetItemRectMax().y - ImGui::GetItemRectMin().y;
  {
    const ImVec2 rmin = ImGui::GetItemRectMin();
    const ImVec2 rmax = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (isSelected)
    {
      dl->AddRectFilled(rmin, ImVec2(rmin.x + 2.5f, rmax.y), theme::u32(theme::accent));
    }

    const float tx = rmin.x + ImGui::GetTreeNodeToLabelSpacing();
    const float cy = (rmin.y + rmax.y) * 0.5f;
    gc::drawSecIcon(dl, ImVec2(tx + 8.0f, cy), 16.0f, iconForObject(object), theme::u32(theme::t2));

    const float textX = tx + 22.0f;
    const float nameMax = rmax.x - textX - 72.0f;
    const std::string shown = gc::ellipsize(object->getName().c_str(), nameMax);
    dl->AddText(ImVec2(textX, cy - ImGui::GetTextLineHeight() * 0.5f), theme::u32(theme::t1), shown.c_str());
  }

  // Drag this node onto another to reparent it (drop on empty space in the window reparents to root).
  // Reparenting is a mutation, so it's only available when the server is editable.
  if (m_editable && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
  {
    const std::string uuidStr = uuids::to_string(object->getUUID());
    ImGui::SetDragDropPayload("object", uuidStr.c_str(), uuidStr.size());
    ImGui::TextUnformatted(object->getName().c_str());
    ImGui::EndDragDropSource();
  }

  if (m_editable && ImGui::BeginDragDropTarget())
  {
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("object"))
    {
      const std::string uuidStr(static_cast<const char*>(payload->Data), payload->DataSize);
      if (const auto dragged = uuids::uuid::from_string(uuidStr); dragged.has_value() && m_sceneEditCallback)
      {
        const auto parent = object->getUUID();
        m_sceneEditCallback(replication::buildReparentObject(dragged.value(), &parent));
      }
    }

    ImGui::EndDragDropTarget();
  }

  // The context menu is mutation-only (add child / duplicate / delete), so it's suppressed when the
  // server is read-only.
  if (m_editable && ImGui::BeginPopupContextItem())
  {
    if (ImGui::MenuItem("Add Child") && m_sceneEditCallback)
    {
      const auto parent = object->getUUID();
      m_sceneEditCallback(replication::buildAddObject("Object", &parent));
    }

    if (ImGui::MenuItem("Duplicate") && m_sceneEditCallback)
    {
      m_sceneEditCallback(replication::buildDuplicateObject(object->getUUID()));
    }

    if (ImGui::MenuItem("Save as Prefab"))
    {
      saveAsPrefab(object);
    }

    if (ImGui::MenuItem("Delete"))
    {
      // Queue the object; displayDeleteConfirmationModal() prompts before actually removing it.
      m_objectPendingDeletion = object->getUUID();
    }

    ImGui::EndPopup();
  }

  // Add/Delete Buttons, flush to the right edge of the row.
  ImGui::SameLine();

  const float buttonWidth = rowHeight;
  constexpr float buttonGap = 3.0f;
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - buttonWidth * 2.0f - buttonGap);

  ImGui::SetNextItemAllowOverlap();
  if (gc::rowIconButton("addChild", gc::SecIcon::plus, false, buttonWidth, rowHeight))
  {
    const auto parent = object->getUUID();
    m_sceneEditCallback(replication::buildAddObject("Object", &parent));
  }

  ImGui::SameLine(0.0f, buttonGap);

  ImGui::SetNextItemAllowOverlap();
  if (gc::rowIconButton("deleteObject", gc::SecIcon::minus, true, buttonWidth, rowHeight))
  {
    // Queue the object; displayDeleteConfirmationModal() prompts before actually removing it.
    m_objectPendingDeletion = object->getUUID();
  }

  if (open && !isLeaf)
  {
    for (const auto& child : object->getChildren())
    {
      displayObjectTree(child);
    }

    ImGui::TreePop();
  }

  ImGui::PopID();
}

void ObjectGUIManager::displayDeleteConfirmationModal(const ObjectManager* objectManager)
{
  if (!m_objectPendingDeletion.has_value())
  {
    return;
  }

  // The pending object can disappear out from under us (a fresh snapshot, or another editor removing
  // it): drop the prompt rather than confirming a stale delete.
  const auto object = objectManager ? objectManager->getObjectByUUID(m_objectPendingDeletion.value()) : nullptr;
  if (!object)
  {
    m_objectPendingDeletion.reset();
    return;
  }

  ImGui::OpenPopup("Delete Object?");

  bool shouldDelete = false;

  if (ImGui::BeginPopupModal("Delete Object?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::TextUnformatted("Are you sure you want to delete");
    ImGui::SameLine();
    ImGui::TextColored(theme::accent, "%s", object->getName().c_str());
    ImGui::SameLine();
    ImGui::TextUnformatted("?");

    ImGui::TextColored(theme::t3, "This action cannot be undone.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Danger-red confirm; neutral cancel.
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
      m_objectPendingDeletion.reset();
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }

  if (shouldDelete)
  {
    if (m_sceneEditCallback)
    {
      m_sceneEditCallback(replication::buildRemoveObject(m_objectPendingDeletion.value()));
    }

    if (m_selection->objectUUID() == m_objectPendingDeletion)
    {
      m_selection->clear();
    }

    m_objectPendingDeletion.reset();
  }
}

void ObjectGUIManager::saveAsPrefab(const std::shared_ptr<Object>& object) const
{
  if (!m_addAssetCallback)
  {
    return;
  }

  // The prefab body is the object's own serialize() blob, carried inline in the asset record - there is no
  // file on disk, so it travels with the project and reaches a server that shares no filesystem with us.
  //
  // Prefabs are keyed by display name: saving over an existing name updates that prefab's body in place
  // (keeping its uuid) rather than adding a second one. The uuid here is only used when the name is new.
  m_addAssetCallback({
    { "assetType", "prefab" },
    { "uuid", newAssetUUID() },
    { "name", object->getName() },
    { "body", object->serialize().dump() }
  });
}
