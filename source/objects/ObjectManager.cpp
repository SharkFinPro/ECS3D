#include "ObjectManager.h"

#include "Object.h"
#include "components/RigidBody.h"
#include "components/collisions/Collider.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <set>

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

void ObjectManager::addObject(std::shared_ptr<Object> object)
{
  object->setManager(this);

  if (object->getComponent(ComponentType::collider))
  {
    const auto collider = std::dynamic_pointer_cast<Collider>(object->getComponent(ComponentType::collider));
    xEdges.emplace_back(object, collider, 0.f, true);
    xEdges.emplace_back(object, collider, 0.f, false);

    yEdges.emplace_back(object, collider, 0.f, true);
    yEdges.emplace_back(object, collider, 0.f, false);

    zEdges.emplace_back(object, collider, 0.f, true);
    zEdges.emplace_back(object, collider, 0.f, false);
  }

  objects.push_back(std::move(object));
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

using PotentialCollision = std::pair<std::shared_ptr<Object>, std::shared_ptr<Object>>;

void ObjectManager::checkCollisions()
{
  std::set<PotentialCollision> xSet, ySet, zSet;

#pragma omp parallel sections num_threads(3)
  {
  #pragma omp section
    {
      for (auto& edge : xEdges) {
        edge.position = edge.collider->getRoughFurthestPoint({edge.isMin ? -1 : 1, 0, 0}).x;
      }

      std::ranges::sort(xEdges, [](const ObjectEdge& a, const ObjectEdge& b) {
        return a.position < b.position;
      });

      std::vector<std::shared_ptr<Object>> touching;
      for (const auto& edge : xEdges)
      {
        if (edge.isMin)
        {
          for (auto& object : touching)
          {
            xSet.emplace(edge.object, object);
          }

          touching.push_back(edge.object);
        }
        else
        {
          std::erase(touching, edge.object);
        }
      }
    }

  #pragma omp section
    {
      for (auto& edge : yEdges) {
        edge.position = edge.collider->getRoughFurthestPoint({0, edge.isMin ? -1 : 1, 0}).y;
      }

      std::ranges::sort(yEdges, [](const ObjectEdge& a, const ObjectEdge& b) {
        return a.position < b.position;
      });

      std::vector<std::shared_ptr<Object>> touching;
      for (const auto& edge : yEdges)
      {
        if (edge.isMin)
        {
          for (auto& object : touching)
          {
            ySet.emplace(edge.object, object);
          }

          touching.push_back(edge.object);
        }
        else
        {
          std::erase(touching, edge.object);
        }
      }
    }

  #pragma omp section
    {
      for (auto& edge : zEdges) {
        edge.position = edge.collider->getRoughFurthestPoint({0, 0, edge.isMin ? -1 : 1}).z;
      }

      std::ranges::sort(zEdges, [](const ObjectEdge& a, const ObjectEdge& b) {
        return a.position < b.position;
      });

      std::vector<std::shared_ptr<Object>> touching;
      for (const auto& edge : zEdges)
      {
        if (edge.isMin)
        {
          for (auto& object : touching)
          {
            zSet.emplace(edge.object, object);
          }

          touching.push_back(edge.object);
        }
        else
        {
          std::erase(touching, edge.object);
        }
      }
    }
  }

  std::vector<PotentialCollision> commonPairs;
  for (const auto& pair : xSet)
  {
    if (ySet.contains(pair) && zSet.contains(pair))
    {
      commonPairs.push_back(pair);
    }
  }

  for (const auto& [obj1, obj2] : commonPairs)
  {
    glm::vec3 mtv;

    const auto rigidBody = std::dynamic_pointer_cast<RigidBody>(obj1->getComponent(ComponentType::rigidBody));
    const auto collider = std::dynamic_pointer_cast<Collider>(obj1->getComponent(ComponentType::collider));

    if (!rigidBody)
    {
      continue;
    }

    if (collider->collidesWith(obj2, &mtv))
    {
      rigidBody->handleCollision(mtv, obj2);
    }
  }
}

  // std::ranges::sort(objects, [](const std::shared_ptr<Object>& a, const std::shared_ptr<Object>& b) {
  //   const auto c1 = std::dynamic_pointer_cast<Collider>(a->getComponent(ComponentType::collider));
  //   const auto c2 = std::dynamic_pointer_cast<Collider>(b->getComponent(ComponentType::collider));
  //
  //   if (!c1 || !c2)
  //   {
  //     return false;
  //   }
  //
  //   constexpr glm::vec3 direction = { -1, 0, 0 };
  //
  //   return c1->getRoughFurthestPoint(direction).x < c2->getRoughFurthestPoint(direction).x;
  // });

// #pragma omp parallel for default(none) num_threads(6)
//   for (int i = 0; i < objects.size(); i++)
//   {
//     const auto object = objects[i];
//
//     auto rigidBody = std::dynamic_pointer_cast<RigidBody>(object->getComponent(ComponentType::rigidBody));
//     auto collider = std::dynamic_pointer_cast<Collider>(object->getComponent(ComponentType::collider));
//
//     if (!rigidBody || !collider)
//     {
//       continue;
//     }
//
//     std::vector<std::shared_ptr<Object>> collidedObjects;
//     findCollisions(object, collider, collidedObjects);
//
//     if (!collidedObjects.empty())
//     {
//       handleCollisions(rigidBody, collider, collidedObjects);
//     }
//   }
// }

void ObjectManager::findCollisions(const std::shared_ptr<Object>& object,
                                   const std::shared_ptr<Collider>& collider,
                                   std::vector<std::shared_ptr<Object>>& collidedObjects) const
{
  for (const auto& other : objects)
  {
    if (other == object)
    {
      continue;
    }

    const auto c2 = std::dynamic_pointer_cast<Collider>(other->getComponent(ComponentType::collider));
    if (!c2)
    {
      continue;
    }

    glm::vec3 direction = { -1, 0, 0 };
    if (c2->getRoughFurthestPoint(direction).x > collider->getRoughFurthestPoint(-direction).x) {
      break;
    }

    direction = {0, 0, -1};
    if (collider->getRoughFurthestPoint(direction).z > c2->getRoughFurthestPoint(-direction).z ||
        collider->getRoughFurthestPoint(-direction).z < c2->getRoughFurthestPoint(direction).z)
    {
      continue;
    }

    direction = {0, -1, 0};
    if (collider->getRoughFurthestPoint(direction).y > c2->getRoughFurthestPoint(-direction).y ||
        collider->getRoughFurthestPoint(-direction).y < c2->getRoughFurthestPoint(direction).y)
    {
      continue;
    }

    if (collider->collidesWith(other, nullptr))
    {
      collidedObjects.emplace_back(other);
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
