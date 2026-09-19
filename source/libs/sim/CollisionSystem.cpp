#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "collisions/NarrowPhase.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/RigidBody.h>
#include <objects/components/Transform.h>
#include <objects/components/collisions/Collider.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <iterator>

namespace {
  // One candidate's narrow-phase result, kept beside its squared penetration depth so
  // CollisionSystem::handleCollisions can sort by depth and then resolve without recomputing the contact.
  struct ScoredContact {
    float distance;
    std::shared_ptr<Object> object;
    std::optional<collisions::Contact> contact;
  };
}

void CollisionSystem::fixedUpdate(const ObjectManager& objectManager)
{
  m_collisionEdges.clear();

  for (const auto& object : objectManager.getAllObjects())
  {
    if (const auto collider = object->getComponent<Collider>(ComponentType::collider))
    {
      // A collider with no Transform has nothing to place it; its accessors throw, which would abandon
      // every other pair's collision work for the tick.
      if (!object->getComponent<Transform>(ComponentType::transform))
      {
        continue;
      }

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

  // This loop has to stay read-only: findCollisions reads bounding boxes and (through the narrow phase)
  // transformed meshes that live on the same Collider another thread's iteration can also read. That is
  // only safe because every collider in m_collisionEdges was just warmed serially above, and nothing in
  // this loop moves a transform - collision responses, which do, are deferred to the serial pass below.
  // Losing either half of that (a collider left cold, or a response sneaking back into this loop) turns
  // it into two threads racing a write on m_boundingBox / m_transformedBoxVertices.
#pragma omp parallel for default(none) shared(perEdgeCollisions) num_threads(6)
  for (int i = 0; i < m_collisionEdges.size(); ++i)
  {
    const auto& edge = m_collisionEdges[i];

    if (!edge.object->getComponent<RigidBody>(ComponentType::rigidBody))
    {
      continue;
    }

    std::vector<std::shared_ptr<Object>> collidedObjects;
    findCollisions(edge, collidedObjects);

    if (!collidedObjects.empty())
    {
      perEdgeCollisions[i] = std::move(collidedObjects);
    }
  }

  // Applied serially, in edge order, now that the parallel region is done: a response moves the transform
  // of either object in a pair, which would invalidate a collider cache another thread might still be
  // reading if this ran inside the loop above.
  for (size_t i = 0; i < m_collisionEdges.size(); ++i)
  {
    if (perEdgeCollisions[i].empty())
    {
      continue;
    }

    const auto rigidBody = m_collisionEdges[i].object->getComponent<RigidBody>(ComponentType::rigidBody);
    if (!rigidBody)
    {
      continue;
    }

    handleCollisions(rigidBody, m_collisionEdges[i].collider, perEdgeCollisions[i]);
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
  current.erase(std::unique(current.begin(), current.end()), current.end());

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
  // cachedBoundingBox(), not getBoundingBox(): this runs inside checkCollisions' parallel loop, where
  // every collider's bounding box was already warmed serially and nothing moves a transform until the
  // serial response pass after the loop. getBoundingBox() would recompute (and write) on a cache miss;
  // cachedBoundingBox() only ever reads, which is what makes concurrent calls on a shared collider safe.
  const auto& bbox = edge.collider->cachedBoundingBox();

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

    // Layer/mask filter: skip pairs that don't share a collision layer before any narrow-phase or event
    // work, so filtered layers produce neither a physical response nor a collision event. Kept after the
    // sweep-and-prune break so the early-out still fires for beyond-range colliders on any layer.
    if (!layersCollide(edge.collider, other.collider))
    {
      continue;
    }

    const auto& otherBbox = other.collider->cachedBoundingBox();

    if (bbox.maxX < otherBbox.minX || bbox.minX > otherBbox.maxX ||
        bbox.maxY < otherBbox.minY || bbox.minY > otherBbox.maxY ||
        bbox.maxZ < otherBbox.minZ || bbox.minZ > otherBbox.maxZ)
    {
      continue;
    }

    if (collisions::intersects(*edge.collider, *other.collider))
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
    // Triggers still recorded the pair (events fire), but get no MTV correction / impulse.
    if (isTriggerPair(collider, collidedObjects[0]))
    {
      return;
    }

    if (const auto contact = contactWith(collider, collidedObjects[0]))
    {
      PhysicsSystem::handleCollision(*rigidBody, collidedObjects[0], contact->minimumTranslationVector,
                                     contact->point);
    }

    return;
  }

  // Each candidate's contact (found once here) alongside its squared penetration depth, so the deepest
  // overlap is resolved first without re-running the narrow phase for the candidate chosen below. A pair
  // with no contact scores zero, which the loop below stops at.
  std::vector<ScoredContact> scoredContacts;
  scoredContacts.reserve(collidedObjects.size());

  for (const auto& collidedObject : collidedObjects)
  {
    auto contact = contactWith(collider, collidedObject);
    const float distance = contact ? dot(contact->minimumTranslationVector, contact->minimumTranslationVector)
                                   : 0.0f;

    scoredContacts.push_back({ distance, collidedObject, std::move(contact) });
  }

  std::ranges::stable_sort(scoredContacts, [](const ScoredContact& a, const ScoredContact& b)
  {
    return a.distance > b.distance;
  });

  for (const auto& scoredContact : scoredContacts)
  {
    if (scoredContact.distance == 0)
    {
      break;
    }

    if (isTriggerPair(collider, scoredContact.object))
    {
      continue;
    }

    if (scoredContact.contact)
    {
      PhysicsSystem::handleCollision(*rigidBody, scoredContact.object, scoredContact.contact->minimumTranslationVector,
                                     scoredContact.contact->point);
    }
  }
}

std::optional<collisions::Contact> CollisionSystem::contactWith(const std::shared_ptr<Collider>& collider,
                                                                const std::shared_ptr<Object>& other)
{
  const auto otherCollider = other->getComponent<Collider>(ComponentType::collider);
  if (!otherCollider)
  {
    return std::nullopt;
  }

  return collisions::findContact(*collider, *otherCollider);
}

bool CollisionSystem::isTriggerPair(const std::shared_ptr<Collider>& collider, const std::shared_ptr<Object>& other)
{
  if (collider->isTrigger())
  {
    return true;
  }

  const auto otherCollider = other->getComponent<Collider>(ComponentType::collider);
  return otherCollider && otherCollider->isTrigger();
}

bool CollisionSystem::layersCollide(const std::shared_ptr<Collider>& a, const std::shared_ptr<Collider>& b)
{
  // layer is 0-31 (clamped in Collider::setLayer), so the shift stays in range.
  const uint32_t aBit = 1u << a->getLayer();
  const uint32_t bBit = 1u << b->getLayer();

  return (a->getMask() & bBit) != 0u && (b->getMask() & aBit) != 0u;
}
