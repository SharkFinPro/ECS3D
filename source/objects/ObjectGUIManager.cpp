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
{}

void ObjectGUIManager::update()
{
  displayObjectListGui();

  reorderObjectGui();
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

void ObjectGUIManager::reorderObjectGui()
{
  for (int i = 0; i < m_objectUINodesSetForReassignment.size(); ++i)
  {
    const auto node = m_objectUINodesSetForReassignment[i];

    if (!isAncestor(node->newParent, node))
    {
      continue;
    }

    for (auto& child : node->children)
    {
      child->newParent = node->parent;
      m_objectUINodesSetForReassignment.push_back(child);
    }
  }

  for (auto& node : m_objectUINodesSetForReassignment)
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

  m_objectUINodesSetForReassignment.clear();
}

void ObjectGUIManager::displayObjectDragDrop(const std::shared_ptr<ObjectUINode>& node)
{
  if (ImGui::BeginDragDropTarget())
  {
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("objectUINode"))
    {
      const auto objectNode = *static_cast<std::shared_ptr<ObjectUINode>*>(payload->Data);

      objectNode->newParent = node;
      m_objectUINodesSetForReassignment.push_back(objectNode);
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

  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + contentRegionWidth - buttonWidth);

  if (ImGui::Button("+", {buttonWidth, 0}))
  {
    const auto newObject = std::make_shared<Object>();
    newObject->setParent(node->object);

    addObject(newObject, node);
    m_objectManager->addObject(newObject);
    m_selectedObject = newObject;
  }
}

void ObjectGUIManager::displayObjectGui(const std::shared_ptr<ObjectUINode>& node)
{
  const auto modelRenderer = node->object->getComponent<ModelRenderer>(ComponentType::modelRenderer);
  const auto renderer = m_objectManager->getECS()->getRenderer();

  if (renderer->getRenderingManager()->getRenderer3D()->getMousePicker()->canMousePick() && renderer->getWindow()->buttonIsPressed(GLFW_MOUSE_BUTTON_LEFT))
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
    if (ImGui::IsItemClicked())
    {
      m_selectedObject = node->object;
    }

    displayObjectDragDrop(node);

    const bool hasChildren = !node->children.empty();

    displayCreateObjectChildButton(node);

    if (hasChildren)
    {
      for (const auto& child : node->children)
      {
        displayObjectGui(child);
      }
    }

    ImGui::TreePop();
  }
  else
  {
    if (ImGui::IsItemClicked())
    {
      m_selectedObject = node->object;
    }

    displayObjectDragDrop(node);

    displayCreateObjectChildButton(node);
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
      m_objectUINodesSetForReassignment.push_back(objectNode);
    }

    ImGui::EndDragDropTarget();
  }

  ImGui::End();
}
