#include "CollisionManager.h"
#include "Object.h"
#include "components/RigidBody.h"
#include "components/collisions/Collider.h"
#include <glm/glm.hpp>
#include <algorithm>

#include "../ECS3D.h"
#include "components/Transform.h"
#include <omp.h>

void CollisionManager::addObject(const std::shared_ptr<Object>& object)
{
  if (const auto collider = object->getComponent<Collider>(ComponentType::collider))
  {
    collisionEdges.push_back({object, collider, 0.0f});
  }
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

void CollisionManager::variableUpdate()
{
  const auto renderer = collisionEdges[0].object->getManager()->getECS()->getRenderer();

  uint32_t linesRendered = 0;
  for (const auto& threadLineVector : threadLines)
  {
    for (const auto& line : threadLineVector)
    {
      renderer->renderLine(line.start, line.end);
      linesRendered++;
      if (linesRendered > 20'000)
      {
        return;
      }
    }
  }
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

  for (auto& threadLineVector : threadLines)
  {
    threadLineVector.clear();
  }

#pragma omp parallel for default(none) num_threads(6)
  for (int i = 0; i < collisionEdges.size(); ++i)
  {
    const auto edge = collisionEdges[i];

    auto rigidBody = edge.object->getComponent<RigidBody>(ComponentType::rigidBody);

    if (!rigidBody)
    {
      continue;
    }

    std::vector<std::shared_ptr<Object>> collidedObjects;
    findCollisions(edge, collidedObjects, threadLines[omp_get_thread_num()]);

    if (!collidedObjects.empty())
    {
      handleCollisions(rigidBody, edge.collider, collidedObjects);
    }
  }
}

void CollisionManager::findCollisions(const LeftEdge& edge,
                                      std::vector<std::shared_ptr<Object>>& collidedObjects,
                                      std::vector<LineToRender>& threadLine) const
{
  const auto a = edge.object->getComponent<Transform>(ComponentType::transform);

  const float furthestX = edge.collider->getRoughFurthestPoint({-1, 0, 0}).x;

  for (const auto& other : collisionEdges)
  {
    if (other.object == edge.object ||
        other.object->getParent() == edge.object ||
        other.object == edge.object->getParent())
    {
      continue;
    }

    if (other.position > furthestX)
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


    const auto b = other.object->getComponent<Transform>(ComponentType::transform);
    if (edge.object->getName() != "Rigid Block" && other.object->getName() != "Rigid Block" && threadLine.size() < 1'500)
    {
      threadLine.push_back({ a->getPosition(), b->getPosition() });
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
  if (collidedObjects.size() == 1)
  {
    glm::vec3 mtv;
    if (collider->collidesWith(collidedObjects[0], &mtv))
    {
      rigidBody->handleCollision(mtv, collidedObjects[0]);
    }

    return;
  }

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
