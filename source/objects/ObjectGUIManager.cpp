#include "ObjectGUIManager.h"
#include "Object.h"
#include <imgui.h>
#include <algorithm>

ObjectGUIManager::ObjectGUIManager(ObjectManager* objectManager)
  : objectManager(objectManager)
{}

void ObjectGUIManager::update()
{
  displayObjectListGui();

  reorderObjectGui();

  displaySelectedObjectGui();
}

void ObjectGUIManager::addObject(const std::shared_ptr<Object>& object,
                                 const std::shared_ptr<ObjectUINode>& parentUINode)
{
  if (containsObjectUINode(objectUINodes, object))
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
    objectUINodes.push_back(uiNode);
  }
}

void ObjectGUIManager::displaySelectedObjectGui() const
{
  ImGui::Begin("Selected Object");

  if (selectedObject)
  {
    selectedObject->displayGui();
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
  for (const auto& node : objectUINodesSetForReassignment)
  {
    if (!isAncestor(node->newParent, node))
    {
      continue;
    }

    for (auto& child : node->children)
    {
      child->newParent = node->parent;
      objectUINodesSetForReassignment.push_back(child);
    }
  }

  for (auto& node : objectUINodesSetForReassignment)
  {
    std::erase(node->parent ? node->parent->children : objectUINodes, node);

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
      objectUINodes.push_back(node);
    }
  }

  objectUINodesSetForReassignment.clear();
}

void ObjectGUIManager::displayObjectDragDrop(const std::shared_ptr<ObjectUINode>& node)
{
  if (ImGui::BeginDragDropTarget())
  {
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("objectUINode"))
    {
      const auto objectNode = *static_cast<std::shared_ptr<ObjectUINode>*>(payload->Data);

      objectNode->newParent = node;
      objectUINodesSetForReassignment.push_back(objectNode);
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
    const auto newObj = std::make_shared<Object>();
    newObj->setParent(node->object);

    addObject(newObj, node);
    objectManager->addObject(newObj);
    selectedObject = newObj;
  }
}

void ObjectGUIManager::displayObjectGui(const std::shared_ptr<ObjectUINode>& node)
{
  ImGui::PushID(&node->object);

  if (ImGui::TreeNodeEx(node->object->getName().c_str(),
                        (node->children.empty() ? ImGuiTreeNodeFlags_Leaf : 0) |
                        (selectedObject == node->object ? ImGuiTreeNodeFlags_Selected : 0) |
                        ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth |
                        ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_OpenOnDoubleClick))
  {
    if (ImGui::IsItemClicked())
    {
      selectedObject = node->object;
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
      selectedObject = node->object;
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
    objectManager->addObject(newObject);
    selectedObject = newObject;
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  if (ImGui::BeginChild("##"))
  {
    for (const auto& node : objectUINodes)
    {
      displayObjectGui(node);
    }

    ImGui::EndChild();
  }

  if (ImGui::BeginDragDropTarget())
  {
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("objectUINode"))
    {
      const auto objectNode = *static_cast<std::shared_ptr<ObjectUINode>*>(payload->Data);

      objectNode->newParent = nullptr;
      objectUINodesSetForReassignment.push_back(objectNode);
    }

    ImGui::EndDragDropTarget();
  }

  ImGui::End();
}
