#include "CollisionManager.h"
#include "Object.h"
#include "components/RigidBody.h"
#include "components/collisions/Collider.h"
#include <glm/glm.hpp>
#include <algorithm>

#ifdef COLLISION_DEBUG
#include "../ECS3D.h"
#include "components/Transform.h"
#include <omp.h>
#endif

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

#ifdef COLLISION_DEBUG
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
#endif

void CollisionManager::checkCollisions()
{
  for (auto& edge : collisionEdges)
  {
    edge.position = edge.collider->getBoundingBox().minX;
  }

  std::ranges::sort(collisionEdges, [](const LeftEdge& a, const LeftEdge& b)
  {
    return a.position < b.position;
  });

#ifdef COLLISION_DEBUG
  for (auto& threadLineVector : threadLines)
  {
    threadLineVector.clear();
  }
#endif

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
#ifdef COLLISION_DEBUG
    findCollisions(edge, collidedObjects, threadLines[omp_get_thread_num()]);
#else
    findCollisions(edge, collidedObjects);
#endif

    if (!collidedObjects.empty())
    {
      handleCollisions(rigidBody, edge.collider, collidedObjects);
    }
  }
}

#ifdef COLLISION_DEBUG
void CollisionManager::findCollisions(const LeftEdge& edge,
                                      std::vector<std::shared_ptr<Object>>& collidedObjects,
                                      std::vector<LineToRender>& threadLine) const
#else
void CollisionManager::findCollisions(const LeftEdge& edge,
                                      std::vector<std::shared_ptr<Object>>& collidedObjects) const
#endif
{
#ifdef COLLISION_DEBUG
  const auto a = edge.object->getComponent<Transform>(ComponentType::transform);
#endif

  const auto bbox = edge.collider->getBoundingBox();

  for (const auto& other : collisionEdges)
  {
    if (other.object == edge.object ||
        other.object->getParent() == edge.object ||
        other.object == edge.object->getParent())
    {
      continue;
    }

    if (other.position > bbox.maxX)
    {
      break;
    }

    const auto otherBbox = other.collider->getBoundingBox();

    if (bbox.maxX < otherBbox.minX || bbox.minX > otherBbox.maxX ||
        bbox.maxY < otherBbox.minY || bbox.minY > otherBbox.maxY ||
        bbox.maxZ < otherBbox.minZ || bbox.minZ > otherBbox.maxZ)
    {
      continue;
    }

#ifdef COLLISION_DEBUG
    const auto b = other.object->getComponent<Transform>(ComponentType::transform);
    if (threadLine.size() < 1'500)
    {
      threadLine.push_back({ a->getPosition(), b->getPosition() });
    }
#endif

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
