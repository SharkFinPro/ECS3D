#include "ObjectManager.h"
#include "Object.h"
#include "components/Component.h"
#include "CollisionManager.h"
#include <imgui.h>

ObjectManager::ObjectManager()
  : ecs(nullptr), collisionManager(std::make_shared<CollisionManager>()), fixedUpdateDt(1.0f / 50.0f),
    timeAccumulator(0.0f), sceneStatus(SceneStatus::stopped)
{}

void ObjectManager::update(const float dt)
{
  displayGui();

  reorderObjectGui();

  fixedUpdate(dt);
  variableUpdate(dt);
}

void ObjectManager::setECS(ECS3D* ecs)
{
  this->ecs = ecs;
}

ECS3D* ObjectManager::getECS() const
{
  return ecs;
}

std::shared_ptr<CollisionManager> ObjectManager::getCollisionManager() const
{
  return collisionManager;
}

void ObjectManager::addObject(const std::shared_ptr<Object>& object, const std::shared_ptr<ObjectUINode>& parentUINode)
{
  object->setManager(this);

  if (object->getComponent(ComponentType::collider))
  {
    collisionManager->addObject(object);
  }

  objects.push_back(object);

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

void ObjectManager::startScene()
{
  if (sceneStatus == SceneStatus::stopped)
  {
    for (const auto& object : objects)
    {
      object->start();
    }
  }

  sceneStatus = SceneStatus::running;
}

void ObjectManager::pauseScene()
{
  sceneStatus = SceneStatus::paused;
}

void ObjectManager::resetScene()
{
  if (sceneStatus == SceneStatus::running || sceneStatus == SceneStatus::paused)
  {
    for (const auto& object : objects)
    {
      object->stop();
    }
  }

  sceneStatus = SceneStatus::stopped;
}

void ObjectManager::variableUpdate(const float dt) const
{
  for (const auto& object : objects)
  {
    object->variableUpdate(dt);
  }
}

void ObjectManager::fixedUpdate(const float dt)
{
  if (sceneStatus != SceneStatus::running)
  {
    return;
  }

  timeAccumulator += dt;

  uint8_t steps = 1;
  while (timeAccumulator >= fixedUpdateDt && steps <= 3)
  {
    steps++;

    for (const auto& object : objects)
    {
      object->fixedUpdate(fixedUpdateDt);
    }

    collisionManager->update();

    timeAccumulator -= fixedUpdateDt;
  }
}

void ObjectManager::reorderObjectGui()
{
  for (const auto& node : objectUINodesSetForReassignment)
  {
    bool isAncestor = false;
    auto parent = node->newParent;
    while (parent)
    {
      if (node == parent)
      {
        isAncestor = true;
        break;
      }

      parent = parent->parent;
    }

    if (!isAncestor)
    {
      continue;
    }

    for (auto& child : node->children)
    {
      child->newParent = node->parent;
      objectUINodesSetForReassignment.push_back(child);
    }
  }

  for (size_t i = objectUINodesSetForReassignment.size(); i > 0; i--)
  {
    auto node = objectUINodesSetForReassignment[i - 1];

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

    objectUINodesSetForReassignment.pop_back();
  }
}

void ObjectManager::displayObjectDragDrop(const std::shared_ptr<ObjectUINode>& node)
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

void ObjectManager::displayCreateObjectChildButton(const std::shared_ptr<ObjectUINode>& node)
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
    selectedObject = newObj;
  }
}

void ObjectManager::displayObjectGui(const std::shared_ptr<ObjectUINode>& node)
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

void ObjectManager::displayObjectListGui()
{
  ImGui::Begin("Objects");

  if (ImGui::Button("Create New Object", {ImGui::GetContentRegionAvail().x, 45}))
  {
    const auto newObject = std::make_shared<Object>();
    addObject(newObject);
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

void ObjectManager::displaySelectedObjectGui() const
{
  ImGui::Begin("Selected Object");

  if (selectedObject)
  {
    selectedObject->displayGui();
  }

  ImGui::End();
}

void ObjectManager::displaySceneStatusGui()
{
  ImGui::Begin("Scene Status");

  constexpr int sceneStatusButtonWidth = 125;

  if (sceneStatus != SceneStatus::running)
  {
    if (ImGui::Button("Start", {sceneStatusButtonWidth, 0}))
    {
      startScene();
    }
  }

  if (sceneStatus == SceneStatus::running || sceneStatus == SceneStatus::paused)
  {
    if (sceneStatus == SceneStatus::paused)
    {
      ImGui::SameLine();
    }

    if (ImGui::Button("Stop", {sceneStatusButtonWidth, 0}))
    {
      resetScene();
    }
  }

  if (sceneStatus == SceneStatus::running)
  {
    ImGui::SameLine();

    if (ImGui::Button("Pause", {sceneStatusButtonWidth, 0}))
    {
      pauseScene();
    }
  }

  ImGui::End();
}

void ObjectManager::displayGui()
{
  displayObjectListGui();

  displaySelectedObjectGui();

  displaySceneStatusGui();
}
