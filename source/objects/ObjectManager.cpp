#include "ObjectManager.h"
#include "Object.h"
#include "components/RigidBody.h"
#include "components/collisions/Collider.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <imgui.h>

ObjectManager::ObjectManager()
  : ecs(nullptr), fixedUpdateDt(1.0f / 50.0f), timeAccumulator(0.0f), sceneStatus(SceneStatus::stopped)
{}

void ObjectManager::update(const float dt)
{
  displayGui();

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

void ObjectManager::addObject(const std::shared_ptr<Object>& object, const std::shared_ptr<ObjectUINode>& parentUINode)
{
  object->setManager(this);

  if (object->getComponent(ComponentType::collider))
  {
    addObjectToCollisions(object);
  }

  objects.push_back(object);

  const auto uiNode = std::make_shared<ObjectUINode>(object, parentUINode);

  if (parentUINode)
  {
    parentUINode->children.push_back(uiNode);
  }
  else
  {
    objectUINodes.push_back(uiNode);
  }
}

void ObjectManager::addObjectToCollisions(const std::shared_ptr<Object> &object)
{
  const auto collider = std::dynamic_pointer_cast<Collider>(object->getComponent(ComponentType::collider));

  collisionEdges.push_back({object, collider, 0.0f});
}

void ObjectManager::removeObjectFromCollisions(const std::shared_ptr<Object>& object)
{
  collisionEdges.erase(std::ranges::remove_if(collisionEdges,
                       [&object](const auto& edge)
                       {
                         return edge.object == object;
                       }).begin(), collisionEdges.end());
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

    checkCollisions();

    timeAccumulator -= fixedUpdateDt;
  }
}

void ObjectManager::checkCollisions()
{
  for (auto& edge : collisionEdges)
  {
    edge.position = edge.collider->getRoughFurthestPoint({-1, 0, 0}).x;
  }

  std::ranges::sort(collisionEdges, [](const LeftEdge& a, const LeftEdge& b)
  {
    return a.position < b.position;
  });

#pragma omp parallel for default(none) num_threads(6)
  for (int i = 0; i < collisionEdges.size(); i++)
  {
    const auto edge = collisionEdges[i];

    auto rigidBody = std::dynamic_pointer_cast<RigidBody>(edge.object->getComponent(ComponentType::rigidBody));

    if (!rigidBody)
    {
      continue;
    }

    std::vector<std::shared_ptr<Object>> collidedObjects;
    findCollisions(edge, collidedObjects);

    if (!collidedObjects.empty())
    {
      handleCollisions(rigidBody, edge.collider, collidedObjects);
    }
  }
}

void ObjectManager::findCollisions(const LeftEdge& edge,
                                   std::vector<std::shared_ptr<Object>>& collidedObjects) const
{
  for (const auto& other : collisionEdges)
  {
    if (other.object == edge.object ||
        other.object->getParent() == edge.object ||
        other.object == edge.object->getParent())
    {
      continue;
    }

    if (other.position > edge.position)
    {
      break;
    }

    glm::vec3 direction = {0, 0, -1};
    if (edge.collider->getRoughFurthestPoint(direction).z > other.collider->getRoughFurthestPoint(-direction).z ||
        edge.collider->getRoughFurthestPoint(-direction).z < other.collider->getRoughFurthestPoint(direction).z)
    {
      continue;
    }

    direction = {0, -1, 0};
    if (edge.collider->getRoughFurthestPoint(direction).y > other.collider->getRoughFurthestPoint(-direction).y ||
        edge.collider->getRoughFurthestPoint(-direction).y < other.collider->getRoughFurthestPoint(direction).y)
    {
      continue;
    }

    if (edge.collider->collidesWith(other.object, nullptr))
    {
      collidedObjects.emplace_back(other.object);
    }
  }
}

void ObjectManager::handleCollisions(const std::shared_ptr<RigidBody>& rigidBody,
                                     const std::shared_ptr<Collider>& collider,
                                     const std::vector<std::shared_ptr<Object>>& collidedObjects)
{
  std::vector<bool> chosenFlags(collidedObjects.size(), false);
  std::vector<float> distances;

  for (const auto& collidedObject : collidedObjects)
  {
    glm::vec3 mtv;
    collider->collidesWith(collidedObject, &mtv);

    distances.push_back(dot(mtv, mtv));
  }

  std::vector<float> sortedDistances = distances;
  std::ranges::sort(sortedDistances, std::greater());

  for (const float sortedDistance : sortedDistances)
  {
    if (sortedDistance == 0)
    {
      break;
    }

    for (size_t j = 0; j < distances.size(); j++)
    {
      if (sortedDistance == distances[j] && !chosenFlags[j])
      {
        chosenFlags[j] = true;

        glm::vec3 mtv;
        if (collider->collidesWith(collidedObjects[j], &mtv))
        {
          rigidBody->handleCollision(mtv, collidedObjects[j]);
        }
      }
    }
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

void ObjectManager::displayGui()
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

  for (const auto& node : objectUINodes)
  {
    displayObjectGui(node);
  }

  ImGui::End();

  reorderObjectGui();

  ImGui::Begin("Selected Object");

  if (selectedObject)
  {
    selectedObject->displayGui();
  }

  ImGui::End();

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
