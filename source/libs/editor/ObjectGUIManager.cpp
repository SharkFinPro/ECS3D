#include "ObjectGUIManager.h"
#include "ComponentEditor.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <imgui.h>
#include <string>

ObjectGUIManager::ObjectGUIManager(std::shared_ptr<ComponentEditor> componentEditor)
  : m_componentEditor(std::move(componentEditor))
{}

void ObjectGUIManager::setEditCallback(EditCallback callback)
{
  m_editCallback = std::move(callback);
}

void ObjectGUIManager::displayGui(ObjectManager& objectManager)
{
  ImGui::Begin("Objects");

  for (const auto& object : objectManager.getObjects())
  {
    displayObjectTree(object);
  }

  ImGui::End();

  displaySelectedObject(objectManager);
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

void ObjectGUIManager::displaySelectedObject(ObjectManager& objectManager)
{
  ImGui::Begin("Selected Object");

  if (m_selectedObject.has_value())
  {
    if (const auto object = objectManager.getObjectByUUID(m_selectedObject.value()))
    {
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

      // TODO: an "Add Component" widget + the per-component "-" delete button currently mutate the
      // TODO:   local replicated object only; structural edits (add/remove component, add/remove/
      // TODO:   reparent object) need their own edit-command types to reach the authoritative server.
    }
    else
    {
      // The selected object went away (e.g. a fresh snapshot replaced the scene).
      m_selectedObject.reset();
    }
  }

  ImGui::End();
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

  ImGui::PopID();
}
