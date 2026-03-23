#include "ObjectGUIManager.h"
#include "Object.h"
#include "components/ModelRenderer.h"
#include "../ECS3D.h"
#include <imgui.h>
#include <VulkanEngine/components/renderingManager/RenderingManager.h>
#include <VulkanEngine/components/renderingManager/renderer3D/MousePicker.h>
#include <VulkanEngine/components/renderingManager/renderer3D/Renderer3D.h>
#include <VulkanEngine/components/window/Window.h>
#include <algorithm>

ObjectGUIManager::ObjectGUIManager(ObjectManager* objectManager)
  : m_objectManager(objectManager)
{
  registerWindowEvents();
}

ObjectGUIManager::~ObjectGUIManager()
{
  m_objectManager->getECS()->getRenderer()->getWindow()->removeListener(m_keyCallbackEventListener);
}

void ObjectGUIManager::update()
{
  displayObjectListGui();

  displayDeleteConfirmationModal();

  processReassignment();

  m_mouseWasPressed = m_objectManager->getECS()->getRenderer()->getWindow()->buttonIsPressed(GLFW_MOUSE_BUTTON_LEFT);
}

void ObjectGUIManager::displaySelectedObjectGui()
{
  ImGui::Begin("Selected Object");

  ImGui::Checkbox("Highlight Object", &m_highlightSelectedObject);

  if (m_selectedObject)
  {
    m_selectedObject->displayGui();

    if (m_highlightSelectedObject)
    {
      if (const auto modelRenderer = m_selectedObject->getComponent<ModelRenderer>(ComponentType::modelRenderer))
      {
        modelRenderer->renderHighlight();
      }
    }
  }

  ImGui::End();
}

bool ObjectGUIManager::isAncestor(const std::shared_ptr<Object>& source,
                                  const std::shared_ptr<Object>& target)
{
  auto current = source;
  while (current)
  {
    if (target == current)
    {
      return true;
    }

    current = current->getParent();
  }

  return false;
}

void ObjectGUIManager::displayObjectDragDrop(const std::shared_ptr<Object>& object)
{
  if (ImGui::BeginDragDropTarget())
  {
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("object"))
    {
      const auto objectPayload = *static_cast<std::shared_ptr<Object>*>(payload->Data);

      m_pendingReassignment = {
        .object = objectPayload,
        .newParent = object
      };
    }

    ImGui::EndDragDropTarget();
  }

  if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
  {
    ImGui::SetDragDropPayload("object", &object, sizeof(object));
    ImGui::Text("Object");
    ImGui::EndDragDropSource();
  }
}

void ObjectGUIManager::displayCreateObjectChildButton(const std::shared_ptr<Object>& object)
{
  ImGui::SameLine();

  const float buttonWidth = ImGui::CalcTextSize("+").x + ImGui::GetStyle().FramePadding.x * 4.0f;
  const float contentRegionWidth = ImGui::GetContentRegionAvail().x;

  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + contentRegionWidth - buttonWidth * 2.0f - 5.0f);

  if (ImGui::Button("+", {buttonWidth, 0}))
  {
    const auto newObject = std::make_shared<Object>();
    newObject->setParent(object);

    m_objectManager->addObject(newObject);

    m_selectedObject = newObject;
  }
}

void ObjectGUIManager::displayDeleteObjectButton(const std::shared_ptr<Object>& object)
{
  ImGui::SameLine();

  const float buttonWidth = ImGui::CalcTextSize("-").x + ImGui::GetStyle().FramePadding.x * 4.0f;
  const float contentRegionWidth = ImGui::GetContentRegionAvail().x;

  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + contentRegionWidth - buttonWidth);

  if (ImGui::Button("-", {buttonWidth, 0}))
  {
    m_objectCheckingForDeletion = object;
  }
}

void ObjectGUIManager::displayObjectGui(const std::shared_ptr<Object>& object)
{
  const auto& children = object->getChildren();

  const auto modelRenderer = object->getComponent<ModelRenderer>(ComponentType::modelRenderer);
  const auto renderer = m_objectManager->getECS()->getRenderer();

  if (!m_mouseWasPressed &&
      renderer->getRenderingManager()->getRenderer3D()->getMousePicker()->canMousePick() &&
      renderer->getWindow()->buttonIsPressed(GLFW_MOUSE_BUTTON_LEFT) &&
      renderer->getWindow()->keyIsPressed(GLFW_KEY_LEFT_CONTROL))
  {
    if (modelRenderer && modelRenderer->selectedByRenderer())
    {
      m_selectedObject = object;
    }
    else if (m_selectedObject == object)
    {
      m_selectedObject = nullptr;
    }
  }

  ImGui::PushID(object.get());

  if (ImGui::TreeNodeEx(object->getName().c_str(),
                        (children.empty() ? ImGuiTreeNodeFlags_Leaf : 0) |
                        (m_selectedObject == object ? ImGuiTreeNodeFlags_Selected : 0) |
                        ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth |
                        ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_OpenOnDoubleClick))
  {
    if (ImGui::IsItemFocused())
    {
      m_focusedObject = object;
    }
    else if (m_focusedObject == object)
    {
      m_focusedObject = nullptr;
    }

    if (ImGui::IsItemClicked())
    {
      m_selectedObject = object;
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
      ImGui::OpenPopup("ItemContextMenu");
    }

    if (ImGui::BeginPopup("ItemContextMenu"))
    {
      if (ImGui::MenuItem("Duplicate"))
      {
        m_objectManager->duplicateObject(object);
      }
      ImGui::EndPopup();
    }

    displayObjectDragDrop(object);

    displayCreateObjectChildButton(object);

    displayDeleteObjectButton(object);

    for (const auto& child : children)
    {
      displayObjectGui(child);
    }

    ImGui::TreePop();
  }

  ImGui::PopID();
}

void ObjectGUIManager::displayObjectListGui()
{
  ImGui::Begin("Objects");

  if (ImGui::Button("Create New Object", {ImGui::GetContentRegionAvail().x, 45}))
  {
    const auto newObject = std::make_shared<Object>();

    m_objectManager->addObject(newObject);

    m_selectedObject = newObject;
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  ImGui::BeginChild("##");
  for (const auto& object : m_objectManager->getObjects())
  {
    displayObjectGui(object);
  }
  ImGui::EndChild();

  if (ImGui::BeginDragDropTarget())
  {
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("object"))
    {
      const auto objectPayload = *static_cast<std::shared_ptr<Object>*>(payload->Data);

      m_pendingReassignment = {
        .object = objectPayload,
        .newParent = nullptr
      };
    }

    ImGui::EndDragDropTarget();
  }

  ImGui::End();
}

void ObjectGUIManager::registerWindowEvents()
{
  const auto ecs = m_objectManager->getECS();

  m_keyCallbackEventListener = ecs->getRenderer()->getWindow()->on<vke::KeyCallbackEvent>([this, ecs](const vke::KeyCallbackEvent& e) {
    if (e.action != GLFW_PRESS)
    {
      return;
    }

    if (m_focusedObject &&
        ecs->keyIsPressed(GLFW_KEY_DELETE))
    {
      m_objectCheckingForDeletion = m_focusedObject;
    }

    if (m_focusedObject &&
        ecs->keyIsPressed(GLFW_KEY_LEFT_CONTROL) &&
        ecs->keyIsPressed(GLFW_KEY_LEFT_SHIFT) &&
        ecs->keyIsPressed(GLFW_KEY_D))
    {
      m_objectManager->duplicateObject(m_focusedObject);
    }

    if (m_objectCheckingForDeletion &&
        ecs->keyIsPressed(GLFW_KEY_ENTER))
    {
      deleteNodeQueriedForDeletion();
    }
  });
}

void ObjectGUIManager::displayDeleteConfirmationModal()
{
  if (!m_objectCheckingForDeletion)
  {
    return;
  }

  bool shouldDelete = false;

  ImGui::OpenPopup("Delete Object?");

  if (ImGui::BeginPopupModal("Delete Object?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::Text("Are you sure you want to delete");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "%s", m_objectCheckingForDeletion->getName().c_str());
    ImGui::SameLine();
    ImGui::Text("?");

    ImGui::Text("This action cannot be undone.");

    ImGui::Separator();

    if (ImGui::Button("Yes", ImVec2(120, 0)))
    {
      shouldDelete = true;
    }

    ImGui::SameLine();

    if (ImGui::Button("No", ImVec2(120, 0)))
    {
      m_objectCheckingForDeletion = nullptr;
    }

    ImGui::EndPopup();
  }

  if (shouldDelete)
  {
    deleteNodeQueriedForDeletion();
  }
}

void ObjectGUIManager::deleteNodeQueriedForDeletion()
{
  m_objectManager->removeObject(m_objectCheckingForDeletion);

  m_objectCheckingForDeletion = nullptr;
}

void ObjectGUIManager::processReassignment()
{
  const auto object = m_pendingReassignment.object;
  if (!object)
  {
    return;
  }

  const auto parent = m_pendingReassignment.newParent;
  if (isAncestor(object, parent))
  {
    m_pendingReassignment = {};

    return;
  }

  if (object->getParent())
  {
    object->getParent()->removeChild(object);
  }
  else
  {
    m_objectManager->removeObjectFromRoot(object);
  }

  object->setParent(parent);

  if (parent)
  {
    parent->addChild(object);
  }
  else
  {
    m_objectManager->addObjectToRoot(object);
  }

  m_pendingReassignment = {};
}
