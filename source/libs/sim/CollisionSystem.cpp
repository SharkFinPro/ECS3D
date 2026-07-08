#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "collisions/Simplex.h"
#include "collisions/Polytope.h"
#include "collisions/Support.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/RigidBody.h>
#include <objects/components/collisions/Collider.h>
#include <objects/components/collisions/SphereCollider.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <iterator>

void CollisionSystem::fixedUpdate(const ObjectManager& objectManager)
{
  m_collisionEdges.clear();

  for (const auto& object : objectManager.getAllObjects())
  {
    if (const auto collider = object->getComponent<Collider>(ComponentType::collider))
    {
      m_collisionEdges.push_back({ object, collider, 0.0f });
    }
  }

  checkCollisions();
}

void CollisionSystem::checkCollisions()
{
  for (auto& edge : m_collisionEdges)
  {
    edge.position = edge.collider->getBoundingBox().minX;
  }

  std::ranges::sort(m_collisionEdges, [](const CollisionEdge& a, const CollisionEdge& b)
  {
    return a.position < b.position;
  });

  // Each edge's collided objects, indexed by edge so the parallel loop can record them lock-free (every
  // thread writes only its own slot). Drained serially into the pair set once the loop finishes.
  std::vector<std::vector<std::shared_ptr<Object>>> perEdgeCollisions(m_collisionEdges.size());

#pragma omp parallel for default(none) shared(perEdgeCollisions) num_threads(6)
  for (int i = 0; i < m_collisionEdges.size(); ++i)
  {
    const auto& edge = m_collisionEdges[i];

    auto rigidBody = edge.object->getComponent<RigidBody>(ComponentType::rigidBody);

    if (!rigidBody)
    {
      continue;
    }

    std::vector<std::shared_ptr<Object>> collidedObjects;
    findCollisions(edge, collidedObjects);

    if (!collidedObjects.empty())
    {
      handleCollisions(rigidBody, edge.collider, collidedObjects);
      perEdgeCollisions[i] = std::move(collidedObjects);
    }
  }

  recordCollisionEvents(perEdgeCollisions);
}

void CollisionSystem::recordCollisionEvents(const std::vector<std::vector<std::shared_ptr<Object>>>& perEdgeCollisions)
{
  // Flatten the per-edge results into this tick's canonical pair set. A dynamic-vs-dynamic contact is
  // detected from both sides, so canonicalize (a < b) and dedupe.
  std::vector<CollisionPair> current;
  for (size_t i = 0; i < perEdgeCollisions.size(); ++i)
  {
    const auto& selfUUID = m_collisionEdges[i].object->getUUID();

    for (const auto& other : perEdgeCollisions[i])
    {
      current.push_back(CollisionPair::make(selfUUID, other->getUUID()));
    }
  }

  std::ranges::sort(current);
  current.erase(std::ranges::unique(current).begin(), current.end());

  // Diff against the previous tick. Both sets are sorted, so the enter/stay/exit split is three linear
  // set operations rather than an O(n^2) rescan.
  m_enters.clear();
  m_stays.clear();
  m_exits.clear();

  std::ranges::set_difference(current, m_previousPairs, std::back_inserter(m_enters));
  std::ranges::set_intersection(current, m_previousPairs, std::back_inserter(m_stays));
  std::ranges::set_difference(m_previousPairs, current, std::back_inserter(m_exits));

  m_previousPairs = std::move(current);
}

void CollisionSystem::reset()
{
  m_previousPairs.clear();
  m_enters.clear();
  m_stays.clear();
  m_exits.clear();
}

void CollisionSystem::findCollisions(const CollisionEdge& edge, std::vector<std::shared_ptr<Object>>& collidedObjects) const
{
  const auto bbox = edge.collider->getBoundingBox();

  for (const auto& other : m_collisionEdges)
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

    if (collidesWith(edge.collider, other.object, nullptr, nullptr))
    {
      collidedObjects.emplace_back(other.object);
    }
  }
}

void CollisionSystem::handleCollisions(const std::shared_ptr<RigidBody>& rigidBody, const std::shared_ptr<Collider>& collider,
                                       const std::vector<std::shared_ptr<Object>>& collidedObjects)
{
  if (collidedObjects.size() == 1)
  {
    glm::vec3 mtv;
    glm::vec3 collisionPoint;
    if (collidesWith(collider, collidedObjects[0], &mtv, &collisionPoint))
    {
      PhysicsSystem::handleCollision(*rigidBody, collidedObjects[0], mtv, collisionPoint);
    }

    return;
  }

  std::vector chosenFlags(collidedObjects.size(), false);
  std::vector<float> distances;

  for (const auto& collidedObject : collidedObjects)
  {
    glm::vec3 mtv;
    collidesWith(collider, collidedObject, &mtv, nullptr);

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
        glm::vec3 collisionPoint;
        if (collidesWith(collider, collidedObjects[j], &mtv, &collisionPoint))
        {
          PhysicsSystem::handleCollision(*rigidBody, collidedObjects[j], mtv, collisionPoint);
        }
      }
    }
  }
}

bool CollisionSystem::collidesWith(const std::shared_ptr<Collider>& collider, const std::shared_ptr<Object>& other,
                                   glm::vec3* mtv, glm::vec3* collisionPoint)
{
  const auto otherCollider = other->getComponent<Collider>(ComponentType::collider);
  if (!otherCollider)
  {
    return false;
  }

  if (collider->getColliderType() == ColliderType::sphereCollider && otherCollider->getColliderType() == ColliderType::sphereCollider)
  {
    return handleSphereToSphereCollision(collider, otherCollider, mtv, collisionPoint);
  }

  Simplex simplex;
  glm::vec3 direction{1, 0, 0};

  auto support = getSupport(collider.get(), otherCollider, normalize(direction));
  simplex.addVertex({support, direction});

  direction *= -1.0f;

  constexpr uint8_t maxIterations = 50;
  uint8_t iteration = 0;
  do
  {
    ++iteration;

    support = getSupport(collider.get(), otherCollider, normalize(direction));

    if (glm::dot(support, direction) < 0)
    {
      return false;
    }

    simplex.addVertex({support, direction});
  } while (iteration < maxIterations && !expandSimplex(simplex, direction));

  if (iteration == maxIterations)
  {
    return false;
  }

  if (mtv == nullptr)
  {
    return true;
  }

  const Polytope polytope(collider.get(), otherCollider, simplex);

  const auto minimumTranslationVector = polytope.getMinimumTranslationVector();

  if (minimumTranslationVector.y == 0 && minimumTranslationVector.x == 0 && minimumTranslationVector.z == 0)
  {
    return false;
  }

  *mtv = -minimumTranslationVector;

  const auto pointOfCollision = polytope.findCollisionPoint();

  if (collisionPoint != nullptr)
  {
    *collisionPoint = pointOfCollision;
  }

  return true;
}

bool CollisionSystem::handleSphereToSphereCollision(const std::shared_ptr<Collider>& collider,
                                                    const std::shared_ptr<Collider>& otherCollider,
                                                    glm::vec3* mtv, glm::vec3* collisionPoint)
{
  const auto sphereA = dynamic_cast<SphereCollider*>(collider.get());
  const auto sphereB = std::dynamic_pointer_cast<SphereCollider>(otherCollider);

  const auto combinedRadius = sphereA->getRadius() + sphereB->getRadius();
  const auto delta = otherCollider->getPosition() - collider->getPosition();

  const float dist = length(delta);

  if (dist >= combinedRadius)
  {
    return false;
  }

  const auto minimumTranslationVector = dist != 0.0f ? -(normalize(delta) * (combinedRadius - dist)) : glm::vec3(0, combinedRadius / 2.0f, 0);

  if (mtv != nullptr)
  {
    *mtv = minimumTranslationVector;
  }

  if (collisionPoint != nullptr)
  {
    const auto direction = -glm::normalize(minimumTranslationVector);
    const auto pointOfCollision = collider->getPosition() + direction * sphereB->getRadius();

    *collisionPoint = pointOfCollision;
  }

  return true;
}

bool CollisionSystem::expandSimplex(Simplex& simplex, glm::vec3& direction)
{
  switch (simplex.size())
  {
    case 2:
      lineCase(simplex, direction);
      return false;
    case 3:
      triangleCase(simplex, direction);
      return false;
    case 4:
      return tetrahedronCase(simplex, direction);
    default:
      return false;
  }
}

void CollisionSystem::lineCase(const Simplex& simplex, glm::vec3& direction)
{
  const auto AB = simplex.getB() - simplex.getA();
  const auto AO = -simplex.getA();

  direction = cross(cross(AB, AO), AB);

  if (glm::dot(direction, direction) == 0)
  {
    direction = cross(AB, {0, 0, 1});
  }
}

void CollisionSystem::triangleCase(Simplex& simplex, glm::vec3& direction)
{
  const auto AB = simplex.getB() - simplex.getA();
  const auto AC = simplex.getC() - simplex.getA();
  const auto AO = -simplex.getA();

  const auto ABperp = cross(cross(AC, AB), AB);
  const auto ACperp = cross(cross(AB, AC), AC);

  if (sameDirection(ABperp, AO))
  {
    simplex.removeC();
    direction = ABperp;
    return;
  }

  if (sameDirection(ACperp, AO))
  {
    simplex.removeB();
    direction = ACperp;
    return;
  }

  glm::vec3 normal = cross(AB, AC);
  direction = sameDirection(normal, AO) ? normal : -normal;
}

bool CollisionSystem::tetrahedronCase(Simplex& simplex, glm::vec3& direction)
{
  const auto A = simplex.getA();
  const auto B = simplex.getB();
  const auto C = simplex.getC();
  const auto D = simplex.getD();

  const auto AB = B - A;
  const auto AC = C - A;
  const auto AD = D - A;
  const auto AO = -A;

  auto ABC = cross(AB, AC);
  auto ACD = cross(AC, AD);
  auto ADB = cross(AD, AB);

  if (sameDirection(ABC, D))
  {
    ABC *= -1;
  }
  if (sameDirection(ACD, B))
  {
    ACD *= -1;
  }
  if (sameDirection(ADB, C))
  {
    ADB *= -1;
  }

  if (sameDirection(ABC, AO))
  {
    simplex.removeD();
    direction = ABC;
    return false;
  }

  if (sameDirection(ACD, AO))
  {
    simplex.removeB();
    direction = ACD;
    return false;
  }

  if (sameDirection(ADB, AO))
  {
    simplex.removeC();
    direction = ADB;
    return false;
  }

  return true;
}
