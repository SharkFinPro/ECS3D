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

  processDeletions();

  processReassignments();

  m_mouseWasPressed = m_objectManager->getECS()->getRenderer()->getWindow()->buttonIsPressed(GLFW_MOUSE_BUTTON_LEFT);
}

void ObjectGUIManager::addObject(const std::shared_ptr<Object>& object,
                                 const std::shared_ptr<ObjectUINode>& parentUINode)
{
  if (containsObjectUINode(m_objectUINodes, object))
  {
    return;
  }

  const auto uiNode = std::make_shared<ObjectUINode>();
  uiNode->object = object;
  uiNode->parent = parentUINode;

  if (parentUINode)
  {
    parentUINode->children.push_back(uiNode);
  }
  else
  {
    m_objectUINodes.push_back(uiNode);
  }
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

bool ObjectGUIManager::containsObjectUINode(const std::vector<std::shared_ptr<ObjectUINode>>& rootNodes,
                                            const std::shared_ptr<Object>& object)
{
  return std::ranges::any_of(rootNodes, [&](const auto& node)
  {
    return node->object == object || containsObjectUINode(node->children, object);
  });
}

bool ObjectGUIManager::isAncestor(const std::shared_ptr<ObjectUINode>& source,
                                  const std::shared_ptr<ObjectUINode>& target)
{
  auto current = source;
  while (current)
  {
    if (target == current)
    {
      return true;
    }

    current = current->parent;
  }

  return false;
}

void ObjectGUIManager::processReassignments()
{
  if (m_pendingReassignments.empty())
  {
    return;
  }

  for (int i = 0; i < m_pendingReassignments.size(); ++i)
  {
    const auto node = m_pendingReassignments[i];

    if (!isAncestor(node->newParent, node))
    {
      continue;
    }

    for (auto& child : node->children)
    {
      child->newParent = node->parent;
      m_pendingReassignments.push_back(child);
    }
  }

  for (auto& node : m_pendingReassignments)
  {
    std::erase(node->parent ? node->parent->children : m_objectUINodes, node);

    if (node->newParent)
    {
      node->parent = node->newParent;
      node->object->setParent(node->newParent->object);
      node->newParent->children.push_back(node);
      node->newParent = nullptr;
    }
    else
    {
      node->parent = nullptr;
      node->object->setParent(nullptr);
      m_objectUINodes.push_back(node);
    }
  }

  m_pendingReassignments.clear();
}

void ObjectGUIManager::processDeletions()
{
  if (m_pendingDeletions.empty())
  {
    return;
  }

  for (const auto& node : m_pendingDeletions)
  {
    for (const auto& child : node->children)
    {
      child->newParent = node->parent;
      m_pendingReassignments.push_back(child);
    }

    if (m_focusedNode == node)
    {
      m_focusedNode = nullptr;
    }

    if (m_selectedObject == node->object)
    {
      m_selectedObject = nullptr;
    }

    std::erase(node->parent ? node->parent->children : m_objectUINodes, node);
  }

  m_pendingDeletions.clear();
}

void ObjectGUIManager::displayObjectDragDrop(const std::shared_ptr<ObjectUINode>& node)
{
  if (ImGui::BeginDragDropTarget())
  {
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("objectUINode"))
    {
      const auto objectNode = *static_cast<std::shared_ptr<ObjectUINode>*>(payload->Data);

      objectNode->newParent = node;
      m_pendingReassignments.push_back(objectNode);
    }

    ImGui::EndDragDropTarget();
  }

  if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
  {
    ImGui::SetDragDropPayload("objectUINode", &node, sizeof(node));
    ImGui::Text("Object");
    ImGui::EndDragDropSource();
  }
}

void ObjectGUIManager::displayCreateObjectChildButton(const std::shared_ptr<ObjectUINode>& node)
{
  ImGui::SameLine();

  const float buttonWidth = ImGui::CalcTextSize("+").x + ImGui::GetStyle().FramePadding.x * 4.0f;
  const float contentRegionWidth = ImGui::GetContentRegionAvail().x;

  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + contentRegionWidth - buttonWidth * 2.0f - 5.0f);

  if (ImGui::Button("+", {buttonWidth, 0}))
  {
    const auto newObject = std::make_shared<Object>();
    newObject->setParent(node->object);

    addObject(newObject, node);
    m_objectManager->addObject(newObject);
    m_selectedObject = newObject;
  }
}

void ObjectGUIManager::displayDeleteObjectButton(const std::shared_ptr<ObjectUINode>& node)
{
  ImGui::SameLine();

  const float buttonWidth = ImGui::CalcTextSize("-").x + ImGui::GetStyle().FramePadding.x * 4.0f;
  const float contentRegionWidth = ImGui::GetContentRegionAvail().x;

  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + contentRegionWidth - buttonWidth);

  if (ImGui::Button("-", {buttonWidth, 0}))
  {
    m_nodeCheckingForDeletion = node;
  }
}

void ObjectGUIManager::displayObjectGui(const std::shared_ptr<ObjectUINode>& node)
{
  const auto modelRenderer = node->object->getComponent<ModelRenderer>(ComponentType::modelRenderer);
  const auto renderer = m_objectManager->getECS()->getRenderer();

  if (!m_mouseWasPressed &&
      renderer->getRenderingManager()->getRenderer3D()->getMousePicker()->canMousePick() &&
      renderer->getWindow()->buttonIsPressed(GLFW_MOUSE_BUTTON_LEFT))
  {
    if (modelRenderer && modelRenderer->selectedByRenderer())
    {
      m_selectedObject = node->object;
    }
    else if (m_selectedObject == node->object)
    {
      m_selectedObject = nullptr;
    }
  }

  ImGui::PushID(&node->object);

  if (ImGui::TreeNodeEx(node->object->getName().c_str(),
                        (node->children.empty() ? ImGuiTreeNodeFlags_Leaf : 0) |
                        (m_selectedObject == node->object ? ImGuiTreeNodeFlags_Selected : 0) |
                        ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth |
                        ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_OpenOnDoubleClick))
  {

    if (ImGui::IsItemFocused())
    {
      m_focusedNode = node;
    }
    else if (m_focusedNode == node)
    {
      m_focusedNode = nullptr;
    }

    if (ImGui::IsItemClicked())
    {
      m_selectedObject = node->object;
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
      ImGui::OpenPopup("ItemContextMenu");
    }

    if (ImGui::BeginPopup("ItemContextMenu"))
    {
      if (ImGui::MenuItem("Duplicate"))
      {
        m_objectManager->duplicateObject(node->object);
      }
      ImGui::EndPopup();
    }

    displayObjectDragDrop(node);

    const bool hasChildren = !node->children.empty();

    displayCreateObjectChildButton(node);

    displayDeleteObjectButton(node);

    if (hasChildren)
    {
      for (const auto& child : node->children)
      {
        displayObjectGui(child);
      }
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
    addObject(newObject);
    m_objectManager->addObject(newObject);
    m_selectedObject = newObject;
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  ImGui::BeginChild("##");
  for (const auto& node : m_objectUINodes)
  {
    displayObjectGui(node);
  }
  ImGui::EndChild();

  if (ImGui::BeginDragDropTarget())
  {
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("objectUINode"))
    {
      const auto objectNode = *static_cast<std::shared_ptr<ObjectUINode>*>(payload->Data);

      objectNode->newParent = nullptr;
      m_pendingReassignments.push_back(objectNode);
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

    if (m_focusedNode &&
        ecs->keyIsPressed(GLFW_KEY_DELETE))
    {
      m_nodeCheckingForDeletion = m_focusedNode;
    }

    if (m_focusedNode &&
        ecs->keyIsPressed(GLFW_KEY_LEFT_CONTROL) &&
        ecs->keyIsPressed(GLFW_KEY_LEFT_SHIFT) &&
        ecs->keyIsPressed(GLFW_KEY_D))
    {
      m_objectManager->duplicateObject(m_focusedNode->object);
    }

    if (m_nodeCheckingForDeletion &&
        ecs->keyIsPressed(GLFW_KEY_ENTER))
    {
      deleteNodeQueriedForDeletion();
    }
  });
}

void ObjectGUIManager::displayDeleteConfirmationModal()
{
  if (!m_nodeCheckingForDeletion)
  {
    return;
  }

  bool shouldDelete = false;

  ImGui::OpenPopup("Delete Object?");

  if (ImGui::BeginPopupModal("Delete Object?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::Text("Are you sure you want to delete");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "%s", m_nodeCheckingForDeletion->object->getName().c_str());
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
      m_nodeCheckingForDeletion = nullptr;
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
  m_objectManager->removeObject(m_nodeCheckingForDeletion->object);

  m_pendingDeletions.push_back(m_nodeCheckingForDeletion);

  m_nodeCheckingForDeletion = nullptr;
}
