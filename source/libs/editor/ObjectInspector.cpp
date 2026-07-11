#include "ObjectInspector.h"
#include "AssetDragDrop.h"
#include "ComponentEditor.h"
#include "GuiComponents.h"
#include <Replication.h>
#include <assets/AssetRegistry.h>
#include <objects/Object.h>
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
  constexpr std::array<AddableComponent, 7> addableComponents {{
    { "Rigid Body",        "RigidBody",        ComponentType::rigidBody,        gc::SecIcon::rigid    },
    { "Model Renderer",    "ModelRenderer",    ComponentType::modelRenderer,    gc::SecIcon::image    },
    { "Light Renderer",    "LightRenderer",    ComponentType::lightRenderer,    gc::SecIcon::light    },
    { "Box Collider",      "Box",              ComponentType::collider,         gc::SecIcon::collider },
    { "Sphere Collider",   "Sphere",           ComponentType::collider,         gc::SecIcon::sphere   },
    { "Player Controller", "PlayerController", ComponentType::playerController, gc::SecIcon::none     },
    { "Camera",            "Camera",           ComponentType::camera,           gc::SecIcon::none     }
  }};

  // Heuristic icon for an object derived from its components (the mockup shows a per-object glyph). The
  // object tree in ObjectGUIManager keeps its own copy of this mapping for the tree rows.
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

ObjectInspector::ObjectInspector(std::shared_ptr<ComponentEditor> componentEditor)
  : m_componentEditor(std::move(componentEditor))
{}

void ObjectInspector::setEditCallback(EditCallback callback)
{
  m_editCallback = std::move(callback);
}

void ObjectInspector::setSceneEditCallback(SceneEditCallback callback)
{
  m_sceneEditCallback = std::move(callback);
}

void ObjectInspector::setAssetRegistry(const AssetRegistry* registry)
{
  m_assetRegistry = registry;
}

void ObjectInspector::setEditable(const bool editable)
{
  m_editable = editable;
}

void ObjectInspector::setShowHighlightToggle(const bool show)
{
  m_showHighlightToggle = show;
}

void ObjectInspector::displayTypeChip(const std::shared_ptr<Object>& object) const
{
  const char* typeLabel = labelForObject(object);
  ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - gc::iconPillWidth(typeLabel));
  gc::iconPill(iconForObject(object), typeLabel);
}

void ObjectInspector::display(const std::shared_ptr<Object>& object)
{
  if (m_showHighlightToggle)
  {
    gc::accentCheckbox("Highlight Object", &m_highlightObject);

    ImGui::Separator();
  }

  // Sync the name buffer when the selection changes. Also fold the shown object back to a closed
  // Add Component list, so a viewport pick (which writes the shared selection directly) matches the
  // tree click's reset.
  if (m_nameEditObjectUUID != object->getUUID())
  {
    m_nameEditObjectUUID = object->getUUID();
    m_showComponentSelector = false;
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
}

void ObjectInspector::displayAddComponent(const std::shared_ptr<Object>& object)
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

void ObjectInspector::displayScriptDragDropArea(const float dropZoneStartY,
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

void ObjectInspector::displayComponent(const uuids::uuid& objectUUID, const std::shared_ptr<Component>& component)
{
  const auto key = componentTypeToString.at(
    component->getSubType() != ComponentType::SubComponentType_none ? component->getSubType() : component->getType());

  ImGui::PushID(component.get());

  if (m_componentEditor->displayGui(key, component) && m_editCallback)
  {
    m_editCallback(objectUUID, component);
  }

  // The header's "-" button marks the component deleted; turn that into a structural removeComponent
  // (sent once - the next snapshot rebuilds the object without it).
  if (component->markedAsDeleted() && !m_pendingRemovals.contains(component.get()) && m_sceneEditCallback)
  {
    m_pendingRemovals.insert(component.get());
    m_sceneEditCallback(replication::buildRemoveComponent(objectUUID, component));
  }

  ImGui::PopID();
}
