#include "ObjectManager.h"

#include "Object.h"
#include "components/RigidBody.h"
#include "components/collisions/Collider.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <set>
#include <unordered_set>

#include "components/ModelRenderer.h"

ObjectManager::ObjectManager()
  : ecs(nullptr), fixedUpdateDt(1.0f / 50.0f), timeAccumulator(0.0f)
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

void ObjectManager::addObject(const std::shared_ptr<Object>& object)
{
  object->setManager(this);

  if (object->getComponent(ComponentType::collider))
  {
    const auto collider = std::dynamic_pointer_cast<Collider>(object->getComponent(ComponentType::collider));

    collisionEdges.emplace_back(object, collider, 0);
  }

  objects.push_back(object);
}

void ObjectManager::resetObjects() const
{
  for (const auto& object : objects)
  {
    object->reset();
  }
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
    if (other.object == edge.object)
    {
      continue;
    }

    glm::vec3 direction = { -1, 0, 0 };
    if (other.collider->getRoughFurthestPoint(direction).x > edge.collider->getRoughFurthestPoint(-direction).x) {
      break;
    }

    direction = {0, 0, -1};
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

void ObjectManager::displayGui()
{
  ImGui::Begin("Objects");
  for (const auto& object : objects)
  {
    ImGui::PushID(&object);

    if (ImGui::Selectable(object->getName().c_str(), selectedObject == object))
    {
      selectedObject = object;
    }

    ImGui::PopID();
  }
  ImGui::End();

  ImGui::Begin("Selected Object");

  if (selectedObject)
  {
    selectedObject->displayGui();
  }

  ImGui::End();
}
