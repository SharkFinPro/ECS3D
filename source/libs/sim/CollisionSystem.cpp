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
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <utility>

namespace {
  // One candidate's narrow-phase result, kept beside its squared penetration depth so
  // CollisionSystem::handleCollisions can sort by depth and then resolve without recomputing the contact.
  struct ScoredContact {
    float distance;
    std::shared_ptr<Object> object;
    std::optional<collisions::Contact> contact;
  };
}

void CollisionSystem::fixedUpdate(const ObjectManager& objectManager, const float dt)
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

  checkCollisions(dt);
}

void CollisionSystem::checkCollisions(const float dt)
{
  for (auto& edge : m_collisionEdges)
  {
    edge.position = edge.collider->getBoundingBox().minX;
  }

  std::ranges::sort(m_collisionEdges, [](const CollisionEdge& a, const CollisionEdge& b)
  {
    return a.position < b.position;
  });

  std::vector<char> isDynamic(m_collisionEdges.size());
  for (size_t i = 0; i < m_collisionEdges.size(); ++i)
  {
    isDynamic[i] = m_collisionEdges[i].object->getComponent<RigidBody>(ComponentType::rigidBody) != nullptr;
  }

  // Each edge's forward-sweep hits (higher edge indices, ascending), indexed by edge so the parallel loop
  // can record them lock-free: every thread writes only its own slot.
  std::vector<std::vector<SweepHit>> forwardHits(m_collisionEdges.size());

  // This loop has to stay read-only: findCollisions reads bounding boxes and (through the narrow phase)
  // transformed meshes that live on the same Collider another thread's iteration can also read. That is
  // only safe because every collider in m_collisionEdges was just warmed serially above, and nothing in
  // this loop moves a transform - collision responses, which do, are deferred to the serial pass below.
  // Losing either half of that (a collider left cold, or a response sneaking back into this loop) turns
  // it into two threads racing a write on m_boundingBox / m_transformedBoxVertices.
  // Dynamic scheduling because the work per edge depends on how many colliders overlap it in x.
#pragma omp parallel for default(none) shared(forwardHits, isDynamic) schedule(dynamic, 8) num_threads(6)
  for (int i = 0; i < m_collisionEdges.size(); ++i)
  {
    findCollisions(i, isDynamic, forwardHits[i]);
  }

  // Each side's own narrow-phase answer goes to that side's list. Walking i upward keeps every list in
  // ascending edge order: the entries from lower edges arrive first, then this edge's own forward hits.
  std::vector<std::vector<std::shared_ptr<Object>>> perEdgeCollisions(m_collisionEdges.size());
  for (size_t i = 0; i < forwardHits.size(); ++i)
  {
    for (const auto& hit : forwardHits[i])
    {
      if (hit.lowerSees)
      {
        perEdgeCollisions[i].emplace_back(m_collisionEdges[hit.other].object);
      }

      if (hit.higherSees)
      {
        perEdgeCollisions[hit.other].emplace_back(m_collisionEdges[i].object);
      }
    }
  }

  // Applied serially, now that the parallel region is done: a response moves the transform of either
  // object in a pair, which would invalidate a collider cache another thread might still be reading if
  // this ran inside the loop above.
  for (const auto i : responseOrder(perEdgeCollisions))
  {
    const auto rigidBody = m_collisionEdges[i].object->getComponent<RigidBody>(ComponentType::rigidBody);
    if (!rigidBody)
    {
      continue;
    }

    handleCollisions(rigidBody, m_collisionEdges[i].collider, perEdgeCollisions[i], dt);
  }

  recordCollisionEvents(perEdgeCollisions);
}

std::vector<size_t> CollisionSystem::responseOrder(
  const std::vector<std::vector<std::shared_ptr<Object>>>& perEdgeCollisions) const
{
  std::vector<std::pair<size_t, float>> bottoms;
  for (size_t i = 0; i < perEdgeCollisions.size(); ++i)
  {
    if (perEdgeCollisions[i].empty())
    {
      continue;
    }

    // A blown-up transform gives a NaN bound, which would break the sort's ordering; resolve it last.
    const float bottom = m_collisionEdges[i].collider->cachedBoundingBox().minY;
    bottoms.emplace_back(i, std::isnan(bottom) ? std::numeric_limits<float>::lowest() : bottom);
  }

  // Highest first, not sweep (minX) order. A response pushes both bodies of a dynamic pair apart, so
  // resolving a body before the one resting on it lets that later push drive it back into its own support
  // for the rest of the tick. Top-down, each push is absorbed by the contact beneath it, and the order no
  // longer depends on horizontal position. Gravity only acts along -y, which is what makes y "up" here.
  std::ranges::stable_sort(bottoms, std::ranges::greater{}, &std::pair<size_t, float>::second);

  std::vector<size_t> order;
  order.reserve(bottoms.size());
  for (const auto& entry : bottoms)
  {
    order.push_back(entry.first);
  }

  return order;
}

void CollisionSystem::recordCollisionEvents(const std::vector<std::vector<std::shared_ptr<Object>>>& perEdgeCollisions)
{
  // Flatten the per-edge results into this tick's canonical pair set. A dynamic-vs-dynamic contact is
  // tested once from each side and the two answers can differ, so canonicalize (a < b) and dedupe.
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

void CollisionSystem::findCollisions(const size_t index, const std::vector<char>& isDynamic,
                                     std::vector<SweepHit>& hits) const
{
  const auto& edge = m_collisionEdges[index];

  // cachedBoundingBox(), not getBoundingBox(): this runs inside checkCollisions' parallel loop, where
  // every collider's bounding box was already warmed serially and nothing moves a transform until the
  // serial response pass after the loop. getBoundingBox() would recompute (and write) on a cache miss;
  // cachedBoundingBox() only ever reads, which is what makes concurrent calls on a shared collider safe.
  const auto& bbox = edge.collider->cachedBoundingBox();

  for (size_t j = index + 1; j < m_collisionEdges.size(); ++j)
  {
    const auto& other = m_collisionEdges[j];

    if (other.position > bbox.maxX)
    {
      break;
    }

    // A pair of static colliders never needs testing.
    if (!isDynamic[index] && !isDynamic[j])
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

    // Layer/mask filter: skip pairs that don't share a collision layer before any narrow-phase or event
    // work, so filtered layers produce neither a physical response nor a collision event.
    if (!layersCollide(edge.collider, other.collider))
    {
      continue;
    }

    if (other.object == edge.object ||
        other.object->getParent() == edge.object ||
        other.object == edge.object->getParent())
    {
      continue;
    }

    // The narrow phase is not symmetric in its arguments, so each dynamic side asks with itself first and
    // keeps its own answer.
    SweepHit hit{ j, false, false };
    hit.lowerSees = isDynamic[index] && collisions::intersects(*edge.collider, *other.collider);
    hit.higherSees = isDynamic[j] && collisions::intersects(*other.collider, *edge.collider);

    if (hit.lowerSees || hit.higherSees)
    {
      hits.push_back(hit);
    }
  }
}

void CollisionSystem::handleCollisions(const std::shared_ptr<RigidBody>& rigidBody, const std::shared_ptr<Collider>& collider,
                                       const std::vector<std::shared_ptr<Object>>& collidedObjects, const float dt)
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
                                     contact->contactPoints(), dt);
    }

    return;
  }

  // Each candidate's contact alongside its squared penetration depth, so the deepest overlap is resolved
  // first. A pair with no contact scores zero, which the loop below stops at.
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

  bool bodyMoved = false;
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

    // Once a response has moved the body, the contacts scored before it no longer describe the overlap -
    // the earlier push may have cleared this one, or changed its depth - so it is measured again.
    const auto contact = bodyMoved ? contactWith(collider, scoredContact.object) : scoredContact.contact;
    if (!contact)
    {
      continue;
    }

    PhysicsSystem::handleCollision(*rigidBody, scoredContact.object, contact->minimumTranslationVector,
                                   contact->contactPoints(), dt);
    bodyMoved = true;
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
