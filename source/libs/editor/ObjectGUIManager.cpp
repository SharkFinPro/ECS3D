#include "ObjectGUIManager.h"
#include "ComponentEditor.h"
#include <Replication.h>
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <nlohmann/json.hpp>
#include <imgui.h>
#include <array>
#include <string>
#include <utility>

namespace {
  // The components the "Add Component" menu can attach, as { display label, ComponentRegistry key }.
  // Transform is omitted (every object already has one); scripts attach via script assets.
  constexpr std::array<std::pair<const char*, const char*>, 5> addableComponents {{
    { "Rigid Body", "RigidBody" },
    { "Model Renderer", "ModelRenderer" },
    { "Light Renderer", "LightRenderer" },
    { "Box Collider", "Box" },
    { "Sphere Collider", "Sphere" }
  }};
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
}

std::optional<uuids::uuid> ObjectGUIManager::getHighlightUUID() const
{
  return m_highlightSelectedObject ? m_selectedObject : std::nullopt;
}

void ObjectGUIManager::displayGui(ObjectManager* objectManager)
{
  ImGui::Begin("Objects");

  ImGui::BeginDisabled(objectManager == nullptr);
  if (ImGui::Button("+ Add Object") && objectManager && m_sceneEditCallback)
  {
    m_sceneEditCallback(replication::buildAddObject("Object"));
  }
  ImGui::EndDisabled();

  ImGui::Separator();

  if (objectManager)
  {
    for (const auto& object : objectManager->getObjects())
    {
      displayObjectTree(object);
    }

    // Drop an object onto the empty area below the tree to reparent it to the root.
    ImGui::Dummy(ImGui::GetContentRegionAvail());
    if (ImGui::BeginDragDropTarget())
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
  // (guarded against firing while a text field is being typed into). The confirmation modal follows.
  if (objectManager && m_selectedObject.has_value() &&
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

  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
  if (isSelected)
  {
    flags |= ImGuiTreeNodeFlags_Selected;
  }
  if (isLeaf)
  {
    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
  }

  const bool open = ImGui::TreeNodeEx(object->getName().c_str(), flags);

  if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
  {
    m_selectedObject = object->getUUID();
  }

  // Drag this node onto another to reparent it (drop on empty space in the window reparents to root).
  if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
  {
    const std::string uuidStr = uuids::to_string(object->getUUID());
    ImGui::SetDragDropPayload("object", uuidStr.c_str(), uuidStr.size());
    ImGui::TextUnformatted(object->getName().c_str());
    ImGui::EndDragDropSource();
  }

  if (ImGui::BeginDragDropTarget())
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

  if (ImGui::BeginPopupContextItem())
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

void ObjectGUIManager::displaySelectedObject(ObjectManager* objectManager)
{
  ImGui::Begin("Selected Object");

  ImGui::Checkbox("Highlight Object", &m_highlightSelectedObject);

  if (objectManager && m_selectedObject.has_value())
  {
    if (const auto object = objectManager->getObjectByUUID(m_selectedObject.value()))
    {
      ImGui::Separator();
      ImGui::TextUnformatted(object->getName().c_str());
      ImGui::Separator();

      for (const auto& [type, component] : object->getComponents())
      {
        displayComponent(object->getUUID(), component);
      }

      for (const auto& script : object->getScripts())
      {
        displayComponent(object->getUUID(), script);
      }

      ImGui::Separator();
      displayAddComponent(object);
    }
    else
    {
      // The selected object went away (e.g. a fresh snapshot replaced the scene).
      m_selectedObject.reset();
    }
  }

  ImGui::End();
}

void ObjectGUIManager::displayDeleteConfirmationModal(ObjectManager* objectManager)
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
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "%s", object->getName().c_str());
    ImGui::SameLine();
    ImGui::TextUnformatted("?");

    ImGui::TextUnformatted("This action cannot be undone.");

    ImGui::Separator();

    if (ImGui::Button("Yes", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter))
    {
      shouldDelete = true;
      ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();

    if (ImGui::Button("No", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
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
  if (ImGui::Button("+ Add Component"))
  {
    ImGui::OpenPopup("AddComponentPopup");
  }

  if (ImGui::BeginPopup("AddComponentPopup"))
  {
    for (const auto& [label, key] : addableComponents)
    {
      if (ImGui::Selectable(label) && m_sceneEditCallback)
      {
        m_sceneEditCallback(replication::buildAddComponent(object->getUUID(), key));
      }
    }

    ImGui::EndPopup();
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
