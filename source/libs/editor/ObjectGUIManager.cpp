#include "ObjectGUIManager.h"
#include "AssetDragDrop.h"
#include "ComponentEditor.h"
#include "GuiComponents.h"
#include <Replication.h>
#include <assets/AssetRegistry.h>
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <nlohmann/json.hpp>
#include <imgui.h>
#include <array>
#include <string>
#include <utility>

namespace {
  // The components the "Add Component" menu can attach.
  // checkType is the ComponentType whose presence on the object hides this entry.
  // Transform is omitted (every object already has one); scripts attach via drag & drop only.
  struct AddableComponent { const char* label; const char* key; ComponentType checkType; gc::SecIcon icon; };
  constexpr std::array<AddableComponent, 5> addableComponents {{
    { "Rigid Body",      "RigidBody",     ComponentType::rigidBody,     gc::SecIcon::rigid    },
    { "Model Renderer",  "ModelRenderer", ComponentType::modelRenderer, gc::SecIcon::image    },
    { "Light Renderer",  "LightRenderer", ComponentType::lightRenderer, gc::SecIcon::light    },
    { "Box Collider",    "Box",           ComponentType::collider,      gc::SecIcon::collider },
    { "Sphere Collider", "Sphere",        ComponentType::collider,      gc::SecIcon::sphere   }
  }};

  // Heuristic icon for an object derived from its components (the mockup shows a per-object glyph).
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

  // The short type label shown in the inspector's selected-object chip (mirrors iconForObject's
  // component heuristic).
  const char* labelForObject(const std::shared_ptr<Object>& object)
  {
    const auto& components = object->getComponents();

    if (components.contains(ComponentType::lightRenderer))
    {
      return "Light";
    }

    if (const auto collider = components.find(ComponentType::collider); collider != components.end())
    {
      if (collider->second->getSubType() == ComponentType::SubComponentType_sphereCollider)
      {
        return "Sphere";
      }
      return components.contains(ComponentType::rigidBody) ? "Rigid Block" : "Block";
    }

    if (components.contains(ComponentType::modelRenderer))
    {
      return "Model";
    }

    return "Object";
  }
}

ObjectGUIManager::ObjectGUIManager(std::shared_ptr<ComponentEditor> componentEditor)
  : m_componentEditor(std::move(componentEditor))
{}

void ObjectGUIManager::setEditCallback(EditCallback callback)
{
  m_editCallback = std::move(callback);
}

void ObjectGUIManager::setSceneEditCallback(SceneEditCallback callback)
{
  m_sceneEditCallback = std::move(callback);
}

void ObjectGUIManager::setSelectedObject(const std::optional<uuids::uuid>& objectUUID)
{
  m_selectedObject = objectUUID;
  m_showComponentSelector = false;
}

void ObjectGUIManager::setEditable(const bool editable)
{
  m_editable = editable;
}

void ObjectGUIManager::setAssetRegistry(const AssetRegistry* registry)
{
  m_assetRegistry = registry;
}

std::optional<uuids::uuid> ObjectGUIManager::getHighlightUUID() const
{
  return m_highlightSelectedObject ? m_selectedObject : std::nullopt;
}

void ObjectGUIManager::displayGui(const ObjectManager* objectManager)
{
  ImGui::Begin("Objects");

  if (!m_editable)
  {
    ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "Read-only - server is not in edit mode");
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

    // Drop an object onto the empty area below the tree to reparent it to the root.
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

      ImGui::EndDragDropTarget();
    }
  }

  // Delete hotkey: while the Objects panel has focus, Delete queues the selected object for removal
  // (guarded against firing while a text field is being typed into, or when read-only). The
  // confirmation modal follows.
  if (m_editable && objectManager && m_selectedObject.has_value() &&
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      !ImGui::GetIO().WantTextInput &&
      ImGui::IsKeyPressed(ImGuiKey_Delete))
  {
    m_objectPendingDeletion = m_selectedObject;
  }

  ImGui::End();

  displaySelectedObject(objectManager);

  displayDeleteConfirmationModal(objectManager);
}

void ObjectGUIManager::displayObjectTree(const std::shared_ptr<Object>& object)
{
  ImGui::PushID(uuids::to_string(object->getUUID()).c_str());

  const bool isSelected = m_selectedObject.has_value() && m_selectedObject.value() == object->getUUID();
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
    setSelectedObject(object->getUUID());
  }

  // Icon + name + accent selection bar, drawn over the (empty-label) node row.
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

    if (ImGui::MenuItem("Delete"))
    {
      // Queue the object; displayDeleteConfirmationModal() prompts before actually removing it.
      m_objectPendingDeletion = object->getUUID();
    }

    ImGui::EndPopup();
  }

  // Add/Delete Buttons, flush to the right edge of the row.
  ImGui::SameLine();

  constexpr float buttonWidth = 31.0f;
  constexpr float buttonGap = 3.0f;
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - buttonWidth * 2.0f - buttonGap);

  ImGui::SetNextItemAllowOverlap();
  if (gc::rowIconButton("addChild", gc::SecIcon::plus, false, buttonWidth))
  {
    const auto parent = object->getUUID();
    m_sceneEditCallback(replication::buildAddObject("Object", &parent));
  }

  ImGui::SameLine(0.0f, buttonGap);

  ImGui::SetNextItemAllowOverlap();
  if (gc::rowIconButton("deleteObject", gc::SecIcon::minus, true, buttonWidth))
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

void ObjectGUIManager::displaySelectedObject(const ObjectManager* objectManager)
{
  ImGui::Begin("Selected Object");

  // Panel header: "SELECTED OBJECT" small-caps label + a right-aligned type chip (mockup).
  const auto selected = (objectManager && m_selectedObject.has_value())
    ? objectManager->getObjectByUUID(m_selectedObject.value()) : nullptr;

  gc::sectionLabel("Selected Object");
  if (selected)
  {
    const char* typeLabel = labelForObject(selected);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - gc::iconPillWidth(typeLabel));
    gc::iconPill(iconForObject(selected), typeLabel);
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // Empty state when nothing is selected — an intentional placeholder rather than a lone checkbox.
  if (!selected)
  {
    if (m_selectedObject.has_value())
    {
      // Selection referenced an object that no longer exists (e.g. a fresh snapshot replaced the scene).
      m_selectedObject.reset();
    }

    gc::emptyState(gc::SecIcon::block, "No object selected",
                   "Select an object from the Objects panel");

    ImGui::End();
    return;
  }

  const auto& object = selected;

  gc::accentCheckbox("Highlight Object", &m_highlightSelectedObject);

  ImGui::Separator();

  // Sync the name buffer when the selection changes.
  if (m_nameEditObjectUUID != object->getUUID())
  {
    m_nameEditObjectUUID = object->getUUID();
    const auto name = object->getName();
    const auto len = std::min(name.size(), m_nameEditBuffer.size() - 1);
    name.copy(m_nameEditBuffer.data(), len);
    m_nameEditBuffer[len] = '\0';
  }

  ImGui::BeginDisabled(!m_editable);
  gc::rowLabel("Name");
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
  if (ImGui::InputText(std::string("##objectName" + to_string(object->getUUID())).c_str(), m_nameEditBuffer.data(), m_nameEditBuffer.size()) &&
      m_sceneEditCallback)
  {
    // Immediately process the edit on key press so that when selecting another object, the name change is not lost.
    m_sceneEditCallback(replication::buildRenameObject(object->getUUID(), m_nameEditBuffer.data()));
  }
  ImGui::EndDisabled();

  ImGui::Separator();

  // Read-only: the component widgets still show their values for inspection, but are disabled so
  // they neither edit nor (via the header "-") remove anything.
  ImGui::BeginDisabled(!m_editable);

  for (const auto& [type, component] : object->getComponents())
  {
    displayComponent(object->getUUID(), component);
  }

  ImGui::Separator();
  displayAddComponent(object);

  ImGui::EndDisabled();

  ImGui::Spacing();
  const float scriptDropZoneStartY = ImGui::GetCursorScreenPos().y;
  gc::sectionLabel("Scripts");

  ImGui::BeginDisabled(!m_editable);

  if (object->getScripts().empty())
  {
    gc::dashedBox("No scripts attached");
  }
  else
  {
    for (const auto& script : object->getScripts())
    {
      displayComponent(object->getUUID(), script);
    }
  }

  ImGui::EndDisabled();

  if (m_editable)
  {
    displayScriptDragDropArea(scriptDropZoneStartY, object);
  }

  ImGui::End();
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

    if (m_selectedObject == m_objectPendingDeletion)
    {
      m_selectedObject.reset();
    }

    m_objectPendingDeletion.reset();
  }
}

void ObjectGUIManager::displayAddComponent(const std::shared_ptr<Object>& object)
{
  if (!m_showComponentSelector)
  {
    if (gc::dashedButton("Add Component", gc::SecIcon::plus))
    {
      m_showComponentSelector = true;
    }
    return;
  }

  // Open state: an accent header button (click to cancel) over an inline list of addable components, so
  // the selector reads as the same control as the dashed button that revealed it.
  if (gc::accentButton("Select Component", gc::SecIcon::plus))
  {
    m_showComponentSelector = false;
  }

  ImGui::Spacing();

  // Count the not-yet-present components so the enclosing popup box can size to its rows.
  int addableCount = 0;
  for (const auto& component : addableComponents)
  {
    if (!object->getComponent<Component>(component.checkType))
    {
      ++addableCount;
    }
  }

  constexpr float rowHeight = 34.0f;
  constexpr float rowGap = 2.0f;
  constexpr float boxPad = 6.0f;
  const float boxHeight = (addableCount > 0 ? addableCount * rowHeight + (addableCount - 1) * rowGap
                                            : rowHeight) + boxPad * 2.0f;

  // Enclose the list in a bordered, rounded surface so it reads as a foldable popup.
  ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::head);
  ImGui::PushStyleColor(ImGuiCol_Border, theme::line2);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 7.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(boxPad, boxPad));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, rowGap));

  ImGui::BeginChild("##addComponentList", ImVec2(0.0f, boxHeight), true);

  if (addableCount == 0)
  {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::t3, "All components added");
  }
  else
  {
    for (const auto& [label, key, checkType, icon] : addableComponents)
    {
      if (object->getComponent<Component>(checkType))
      {
        continue;
      }

      if (gc::menuRow(label, icon, rowHeight) && m_sceneEditCallback)
      {
        m_sceneEditCallback(replication::buildAddComponent(object->getUUID(), key));
        m_showComponentSelector = false;
      }
    }
  }

  ImGui::EndChild();

  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor(2);
}

void ObjectGUIManager::displayScriptDragDropArea(const float dropZoneStartY,
                                                  const std::shared_ptr<Object>& object) const
{
  if (ImGui::GetDragDropPayload() == nullptr)
  {
    return;
  }

  const ImVec2 windowPos     = ImGui::GetWindowPos();
  const float  windowRight   = windowPos.x + ImGui::GetWindowWidth();
  const float  contentBottom = windowPos.y + ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y;

  ImGui::SetCursorScreenPos({ windowPos.x, dropZoneStartY });
  ImGui::SetNextItemAllowOverlap();
  ImGui::InvisibleButton("##scriptDropZone",
      { windowRight - windowPos.x, contentBottom - dropZoneStartY });

  if (ImGui::BeginDragDropTarget())
  {
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(assetDragDrop::script))
    {
      const std::string uuidStr(static_cast<const char*>(payload->Data), payload->DataSize);
      if (const auto parsed = uuids::uuid::from_string(uuidStr))
      {
        if (m_assetRegistry)
        {
          if (const auto* record = m_assetRegistry->getByUUID(parsed.value()))
          {
            if (!record->className.empty() && m_sceneEditCallback)
            {
              m_sceneEditCallback(replication::buildAddScript(object->getUUID(), record->className));
            }
          }
        }
      }
    }

    ImGui::EndDragDropTarget();
  }
}

void ObjectGUIManager::displayComponent(const uuids::uuid& objectUUID, const std::shared_ptr<Component>& component)
{
  const auto key = componentTypeToString.at(
    component->getSubType() != ComponentType::SubComponentType_none ? component->getSubType() : component->getType());

  ImGui::PushID(component.get());

  if (m_componentEditor->displayGui(key, component) && m_editCallback)
  {
    m_editCallback(objectUUID, component);
  }

  // The header's "-" button marks the component deleted; turn that into a structural removeComponent
  // (sent once — the next snapshot rebuilds the object without it).
  if (component->markedAsDeleted() && !m_pendingRemovals.contains(component.get()) && m_sceneEditCallback)
  {
    m_pendingRemovals.insert(component.get());
    m_sceneEditCallback(replication::buildRemoveComponent(objectUUID, component));
  }

  ImGui::PopID();
}
