#include "CollisionManager.h"
#include "Object.h"
#include "components/RigidBody.h"
#include "components/collisions/Collider.h"
#include <glm/glm.hpp>
#include <algorithm>

void CollisionManager::addObject(const std::shared_ptr<Object>& object)
{
  const auto colliderComponent = object->getComponent(ComponentType::collider);
  if (!colliderComponent)
  {
    return;
  }

  const auto collider = std::dynamic_pointer_cast<Collider>(colliderComponent);

  collisionEdges.push_back({object, collider, 0.0f});
}

void CollisionManager::removeObject(const std::shared_ptr<Object>& object)
{
  for (auto it = collisionEdges.begin(); it != collisionEdges.end(); ++it)
  {
    if (it->object == object)
    {
      collisionEdges.erase(it);
    }
  }
}

void CollisionManager::update()
{
  checkCollisions();
}

void CollisionManager::checkCollisions()
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

void CollisionManager::findCollisions(const LeftEdge& edge,
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

void CollisionManager::handleCollisions(const std::shared_ptr<RigidBody>& rigidBody,
                                        const std::shared_ptr<Collider>& collider,
                                        const std::vector<std::shared_ptr<Object>>& collidedObjects)
{
  std::vector chosenFlags(collidedObjects.size(), false);
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
