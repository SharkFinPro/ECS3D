#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "collisions/NarrowPhase.h"
#include <ProfileScope.h>
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/RigidBody.h>
#include <objects/components/Transform.h>
#include <objects/components/collisions/Collider.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <exception>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <utility>

namespace {
  // One candidate's narrow-phase result, kept beside its squared penetration depth so
  // CollisionSystem::handleCollisions can sort by depth and then resolve without recomputing the contact.
  struct ScoredContact {
    float distance;
    size_t touch;
    std::optional<collisions::Contact> contact;
  };

  // Colliders that belong to one body never touch each other: they move together, and a response between
  // them would push the body against itself. Static colliders (no body) are not one assembly.
  bool partOfSameAssembly(const CollisionEdge& a, const CollisionEdge& b)
  {
    if (a.object == b.object)
    {
      return true;
    }

    if (a.body && a.body == b.body)
    {
      return true;
    }

    return a.object->isAncestorOf(b.object) || b.object->isAncestorOf(a.object);
  }

  bool boundsOverlap(const BoundingBox& a, const BoundingBox& b)
  {
    return a.maxX >= b.minX && a.minX <= b.maxX &&
           a.maxY >= b.minY && a.minY <= b.maxY &&
           a.maxZ >= b.minZ && a.minZ <= b.maxZ;
  }

  Aabb toAabb(const BoundingBox& bounds)
  {
    return { { bounds.minX, bounds.minY, bounds.minZ }, { bounds.maxX, bounds.maxY, bounds.maxZ } };
  }

  // A blown-up transform gives a NaN bound, which has no place in an ordering; it sorts after every number.
  float orderable(const float value, const float nanValue)
  {
    return std::isnan(value) ? nanValue : value;
  }

  // The rigid body getComponent would hand back for the object, its own or an ancestor's, without the cast.
  const Component* bodyOf(const Object& object)
  {
    return object.getComponent<Component>(ComponentType::rigidBody).get();
  }
}

void CollisionSystem::fixedUpdate(const ObjectManager& objectManager, const float dt)
{
  {
    ECS3D_PROFILE_SCOPE("Broad phase");

    {
      ECS3D_PROFILE_SCOPE("Gather");
      gatherEdges(objectManager);
    }

    {
      ECS3D_PROFILE_SCOPE("Bounds");
      warmEdges();
    }

    {
      ECS3D_PROFILE_SCOPE("Tree update");
      for (const auto& edge : m_collisionEdges)
      {
        m_broadPhase.moveProxy(edge.proxy, toAabb(edge.bounds));
      }
    }

    {
      ECS3D_PROFILE_SCOPE("Pair update");
      m_broadPhase.updatePairs();
    }
  }

  checkCollisions(dt);
}

void CollisionSystem::warmEdges()
{
  // Named, non-const locals so the OpenMP clause below can list them on every compiler.
  auto& edges = m_collisionEdges;
  auto& sweepKeys = m_sweepKeys;
  sweepKeys.resize(edges.size());
  int edgeCount = static_cast<int>(edges.size());
  std::exception_ptr failure;

  // Safe in parallel because each edge is a different collider and getBoundingBox writes only that collider's
  // own caches: a transform, its parents' included, is only read. A throw cannot leave an OpenMP region, so the
  // first one is carried out and rethrown, abandoning the tick as a serial pass would.
#pragma omp parallel for default(none) shared(edges, sweepKeys, edgeCount, failure) schedule(static, 64) \
  num_threads(6) if(edgeCount >= 256)
  for (int i = 0; i < edgeCount; ++i)
  {
    try
    {
      auto& edge = edges[static_cast<size_t>(i)];
      edge.bounds = edge.collider->getBoundingBox();
      edge.layer = edge.collider->getLayer();
      edge.mask = edge.collider->getMask();
      sweepKeys[static_cast<size_t>(i)] = orderable(edge.bounds.minX, std::numeric_limits<float>::infinity());
    }
    catch (...)
    {
#pragma omp critical
      {
        if (!failure)
        {
          failure = std::current_exception();
        }
      }
    }
  }

  if (failure)
  {
    std::rethrow_exception(failure);
  }
}

void CollisionSystem::gatherEdges(const ObjectManager& objectManager)
{
  if (!edgesMatch(objectManager))
  {
    rebuildEdges(objectManager);
  }
}

bool CollisionSystem::edgesMatch(const ObjectManager& objectManager) const
{
  // The edges hold their objects and components alive, so an address seen here cannot belong to a different
  // object than the one the edge was built for.
  size_t index = 0;
  for (const auto& object : objectManager.getAllObjects())
  {
    const auto& components = object->getComponents();
    const auto collider = components.find(ComponentType::collider);
    if (collider == components.end() || !collider->second || !components.contains(ComponentType::transform))
    {
      continue;
    }

    if (index == m_collisionEdges.size())
    {
      return false;
    }

    const auto& edge = m_collisionEdges[index++];
    if (edge.proxy == BroadPhase::nullProxy || edge.object != object ||
        static_cast<const Component*>(edge.collider.get()) != collider->second.get() ||
        static_cast<const Component*>(edge.body.get()) != bodyOf(*object))
    {
      return false;
    }
  }

  return index == m_collisionEdges.size();
}

void CollisionSystem::rebuildEdges(const ObjectManager& objectManager)
{
  // A collider keeps its proxy, and so its cached pairs, while it keeps its body; gaining or losing one
  // moves it between the static and dynamic trees, which takes a new proxy.
  std::unordered_map<const Collider*, size_t> previous;
  previous.reserve(m_collisionEdges.size());
  for (size_t i = 0; i < m_collisionEdges.size(); ++i)
  {
    previous.emplace(m_collisionEdges[i].collider.get(), i);
  }

  std::vector<char> kept(m_collisionEdges.size(), 0);
  std::vector<CollisionEdge> edges;
  edges.reserve(m_collisionEdges.size());

  for (const auto& object : objectManager.getAllObjects())
  {
    const auto collider = object->getComponent<Collider>(ComponentType::collider);
    if (!collider)
    {
      continue;
    }

    // A collider with no Transform has nothing to place it; its accessors throw, which would abandon every
    // other pair's collision work for the tick.
    if (!object->getComponents().contains(ComponentType::transform))
    {
      continue;
    }

    CollisionEdge edge{ object, collider, object->getComponent<RigidBody>(ComponentType::rigidBody) };

    if (const auto found = previous.find(collider.get()); found != previous.end())
    {
      const auto& old = m_collisionEdges[found->second];
      if (!kept[found->second] && (old.body != nullptr) == (edge.body != nullptr))
      {
        kept[found->second] = 1;
        edge.proxy = old.proxy;
      }
    }

    edges.push_back(std::move(edge));
  }

  for (size_t i = 0; i < m_collisionEdges.size(); ++i)
  {
    if (!kept[i])
    {
      m_broadPhase.destroyProxy(m_collisionEdges[i].proxy);
    }
  }

  m_collisionEdges = std::move(edges);

  for (size_t i = 0; i < m_collisionEdges.size(); ++i)
  {
    auto& edge = m_collisionEdges[i];
    if (edge.proxy == BroadPhase::nullProxy)
    {
      edge.proxy = m_broadPhase.createProxy(toAabb(edge.collider->getBoundingBox()), edge.body != nullptr,
                                            static_cast<int32_t>(i));
    }
    else
    {
      m_broadPhase.setUserData(edge.proxy, static_cast<int32_t>(i));
    }
  }
}

void CollisionSystem::checkCollisions(const float dt)
{
  const size_t edgeCount = m_collisionEdges.size();

  if (m_touches.size() < edgeCount)
  {
    m_touches.resize(edgeCount);
  }

  for (size_t i = 0; i < edgeCount; ++i)
  {
    m_touches[i].clear();
  }

  // Named, non-const locals so the OpenMP clause below can list them on every compiler. Each pair's result has
  // its own slot, so the parallel loop records them lock-free.
  auto pairs = m_broadPhase.getPairs();
  auto& pairHits = m_pairHits;
  pairHits.resize(pairs.size());
  int pairCount = static_cast<int>(pairs.size());

  // This loop has to stay read-only: testPair reads bounding boxes and (through the narrow phase) transformed
  // meshes that live on the same Collider another thread's iteration can also read. That is only safe because
  // every collider in m_collisionEdges was just warmed by warmEdges, and nothing in this loop moves
  // a transform - collision responses, which do, are deferred to the serial pass below. Losing either half of
  // that (a collider left cold, or a response sneaking back into this loop) turns it into two threads racing a
  // write on m_boundingBox / the collider's support mesh.
  {
    ECS3D_PROFILE_SCOPE("Narrow phase");

#pragma omp parallel for default(none) shared(pairHits, pairs, pairCount) schedule(dynamic, 32) num_threads(6)
    for (int i = 0; i < pairCount; ++i)
    {
      testPair(pairs[static_cast<size_t>(i)], pairHits[static_cast<size_t>(i)]);
    }
  }

  {
    ECS3D_PROFILE_SCOPE("Response");

    {
      ECS3D_PROFILE_SCOPE("Touches");
      collectTouches();
    }

    // Applied serially, now that the parallel region is done: a response moves the transform of either
    // object in a pair, which would invalidate a collider cache another thread might still be reading if
    // this ran inside the loop above.
    for (const auto i : responseOrder())
    {
      if (!m_collisionEdges[i].body)
      {
        continue;
      }

      handleCollisions(i, dt);
    }

    recordCollisionEvents();
  }
}

void CollisionSystem::testPair(const uint64_t pair, PairHit& hit) const
{
  hit.first = static_cast<size_t>(m_broadPhase.getUserData(BroadPhase::firstOf(pair)));
  hit.second = static_cast<size_t>(m_broadPhase.getUserData(BroadPhase::secondOf(pair)));
  hit.firstSees = false;
  hit.secondSees = false;
  hit.firstWitness.reset();
  hit.secondWitness.reset();

  const auto& edge = m_collisionEdges[hit.first];
  const auto& other = m_collisionEdges[hit.second];

  // The cache pairs fat boxes; the bounds copied onto the edges in fixedUpdate say whether the colliders
  // themselves overlap this tick.
  if (!boundsOverlap(edge.bounds, other.bounds))
  {
    return;
  }

  // Layer/mask filter: skip pairs that don't share a collision layer before any narrow-phase or event
  // work, so filtered layers produce neither a physical response nor a collision event.
  if (!layersCollide(edge, other) || partOfSameAssembly(edge, other))
  {
    return;
  }

  // The narrow phase is not symmetric in its arguments, so each dynamic side asks with itself first and
  // keeps its own answer.
  hit.firstSees = edge.body && collisions::intersects(*edge.collider, *other.collider, hit.firstWitness);
  hit.secondSees = other.body && collisions::intersects(*other.collider, *edge.collider, hit.secondWitness);
}

bool CollisionSystem::sweepsBefore(const size_t a, const size_t b) const
{
  return m_sweepKeys[a] < m_sweepKeys[b] || (m_sweepKeys[a] == m_sweepKeys[b] && a < b);
}

void CollisionSystem::collectTouches()
{
  for (auto& hit : m_pairHits)
  {
    if (hit.firstSees)
    {
      m_touches[hit.first].push_back({ hit.second, std::move(hit.firstWitness) });
    }

    if (hit.secondSees)
    {
      m_touches[hit.second].push_back({ hit.first, std::move(hit.secondWitness) });
    }
  }

  // A body resolves its contacts deepest first, and contacts of equal depth in the order a sweep over min x
  // meets the other side; the stable depth sort in handleCollisions relies on this order.
  for (size_t i = 0; i < m_collisionEdges.size(); ++i)
  {
    auto& touches = m_touches[i];
    if (touches.size() > 1)
    {
      std::ranges::sort(touches, [this](const Touch& a, const Touch& b) { return sweepsBefore(a.other, b.other); });
    }
  }
}

std::vector<size_t> CollisionSystem::responseOrder() const
{
  struct Bottom {
    float minY;
    float sweepKey;
    size_t index;
  };

  std::vector<Bottom> bottoms;
  for (size_t i = 0; i < m_collisionEdges.size(); ++i)
  {
    if (m_touches[i].empty())
    {
      continue;
    }

    // A blown-up transform gives a NaN bound, which would break the sort's ordering; resolve it last.
    bottoms.push_back({ orderable(m_collisionEdges[i].bounds.minY, std::numeric_limits<float>::lowest()),
                        m_sweepKeys[i], i });
  }

  // Highest first, not sweep (minX) order. A response pushes both bodies of a dynamic pair apart, so
  // resolving a body before the one resting on it lets that later push drive it back into its own support
  // for the rest of the tick. Top-down, each push is absorbed by the contact beneath it, and the order no
  // longer depends on horizontal position. Gravity only acts along -y, which is what makes y "up" here.
  // Bodies at the same height go in sweep order.
  std::ranges::sort(bottoms, [](const Bottom& a, const Bottom& b) {
    if (a.minY != b.minY)
    {
      return a.minY > b.minY;
    }

    if (a.sweepKey != b.sweepKey)
    {
      return a.sweepKey < b.sweepKey;
    }

    return a.index < b.index;
  });

  std::vector<size_t> order;
  order.reserve(bottoms.size());
  for (const auto& entry : bottoms)
  {
    order.push_back(entry.index);
  }

  return order;
}

void CollisionSystem::recordCollisionEvents()
{
  // Flatten the per-edge results into this tick's canonical pair set. A dynamic-vs-dynamic contact is
  // tested once from each side and the two answers can differ, so canonicalize (a < b) and dedupe.
  std::vector<CollisionPair> current;
  for (size_t i = 0; i < m_collisionEdges.size(); ++i)
  {
    const auto& selfUUID = m_collisionEdges[i].object->getUUID();

    for (const auto& touch : m_touches[i])
    {
      current.push_back(CollisionPair::make(selfUUID, m_collisionEdges[touch.other].object->getUUID()));
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

  m_collisionEdges.clear();
  m_broadPhase.clear();
  m_pairHits.clear();
}

void CollisionSystem::handleCollisions(const size_t index, const float dt) const
{
  const auto& edge = m_collisionEdges[index];
  const auto& rigidBody = edge.body;
  const auto& collider = edge.collider;
  const auto& touches = m_touches[index];

  // The edge already holds the other side's collider, so the response asks the narrow phase directly.
  const auto contactWith = [&](const Touch& touch) {
    return collisions::findContact(*collider, *m_collisionEdges[touch.other].collider, touch.witness);
  };

  if (touches.size() == 1)
  {
    const auto& other = m_collisionEdges[touches[0].other];

    // Triggers still recorded the pair (events fire), but get no MTV correction / impulse.
    if (isTriggerPair(*collider, *other.collider))
    {
      return;
    }

    if (const auto contact = contactWith(touches[0]))
    {
      PhysicsSystem::handleCollision(*rigidBody, collider, other.object, contact->minimumTranslationVector,
                                     contact->contactPoints(), dt);
    }

    return;
  }

  // Each candidate's contact alongside its squared penetration depth, so the deepest overlap is resolved
  // first. A pair with no contact scores zero, which the loop below stops at.
  std::vector<ScoredContact> scoredContacts;
  scoredContacts.reserve(touches.size());

  for (size_t i = 0; i < touches.size(); ++i)
  {
    auto contact = contactWith(touches[i]);
    const float distance = contact ? dot(contact->minimumTranslationVector, contact->minimumTranslationVector)
                                   : 0.0f;

    scoredContacts.push_back({ distance, i, std::move(contact) });
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

    const auto& touch = touches[scoredContact.touch];
    const auto& other = m_collisionEdges[touch.other];

    if (isTriggerPair(*collider, *other.collider))
    {
      continue;
    }

    // Once a response has moved the body, the contacts scored before it no longer describe the overlap -
    // the earlier push may have cleared this one, or changed its depth - so it is measured again.
    const auto contact = bodyMoved ? contactWith(touch) : scoredContact.contact;
    if (!contact)
    {
      continue;
    }

    PhysicsSystem::handleCollision(*rigidBody, collider, other.object, contact->minimumTranslationVector,
                                   contact->contactPoints(), dt);
    bodyMoved = true;
  }
}

bool CollisionSystem::isTriggerPair(const Collider& collider, const Collider& other)
{
  return collider.isTrigger() || other.isTrigger();
}

bool CollisionSystem::layersCollide(const CollisionEdge& a, const CollisionEdge& b)
{
  // layer is 0-31 (clamped in Collider::setLayer), so the shift stays in range.
  const uint32_t aBit = 1u << a.layer;
  const uint32_t bBit = 1u << b.layer;

  return (a.mask & bBit) != 0u && (b.mask & aBit) != 0u;
}
