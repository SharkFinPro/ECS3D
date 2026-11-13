#include "CollisionSystem.h"
#include "ContactSolver.h"
#include "PhysicsSystem.h"
#include "collisions/FaceManifold.h"
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
#include <functional>
#include <iterator>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <utility>

namespace {
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
}

void CollisionSystem::fixedUpdate(const ObjectManager& objectManager, const float dt)
{
  {
    ECS3D_PROFILE_SCOPE("Broad phase");

    m_collisionEdges.clear();

    for (const auto& object : objectManager.getAllObjects())
    {
      if (const auto collider = object->getComponent<Collider>(ComponentType::collider))
      {
        // A collider with no Transform has nothing to place it; its accessors throw, which would abandon
        // every other pair's collision work for the tick.
        if (!object->getComponents().contains(ComponentType::transform))
        {
          continue;
        }

        m_collisionEdges.push_back({ object, collider, 0.0f, object->getComponent<RigidBody>(ComponentType::rigidBody) });
      }
    }
  }

  checkCollisions(dt);
}

void CollisionSystem::checkCollisions(const float dt)
{
  const size_t edgeCount = m_collisionEdges.size();

  // Named locals so the OpenMP clause below can list them. Each edge's forward-sweep hits (higher edge
  // indices, ascending) are indexed by edge so the parallel loop can record them lock-free: every thread
  // writes only its own slot.
  auto& forwardHits = m_forwardHits;

  if (forwardHits.size() < edgeCount)
  {
    forwardHits.resize(edgeCount);
  }
  if (m_touches.size() < edgeCount)
  {
    m_touches.resize(edgeCount);
  }

  for (size_t i = 0; i < edgeCount; ++i)
  {
    forwardHits[i].clear();
    m_touches[i].clear();
  }

  {
    ECS3D_PROFILE_SCOPE("Broad phase");

    for (auto& edge : m_collisionEdges)
    {
      edge.bounds = edge.collider->getBoundingBox();
      edge.sweep = edge.body ? edge.body->getVelocity() : glm::vec3(0);
      edge.bounds.minX += std::min(edge.sweep.x, 0.0f);
      edge.bounds.maxX += std::max(edge.sweep.x, 0.0f);
      edge.bounds.minY += std::min(edge.sweep.y, 0.0f);
      edge.bounds.maxY += std::max(edge.sweep.y, 0.0f);
      edge.bounds.minZ += std::min(edge.sweep.z, 0.0f);
      edge.bounds.maxZ += std::max(edge.sweep.z, 0.0f);
      edge.position = edge.bounds.minX;
      edge.layer = edge.collider->getLayer();
      edge.mask = edge.collider->getMask();
    }

    std::ranges::sort(m_collisionEdges, [](const CollisionEdge& a, const CollisionEdge& b)
    {
      return a.position < b.position;
    });

    m_sweepBoxes.resize(edgeCount);
    for (size_t i = 0; i < edgeCount; ++i)
    {
      const auto& edge = m_collisionEdges[i];
      m_sweepBoxes[i] = { edge.bounds, edge.layer, edge.mask, edge.body != nullptr };
    }
  }

  // This loop has to stay read-only: findCollisions reads bounding boxes and (through the narrow phase)
  // transformed meshes that live on the same Collider another thread's iteration can also read. That is
  // only safe because every collider in m_collisionEdges was just warmed serially above, and nothing in
  // this loop moves a transform - collision responses, which do, are deferred to the island pass below.
  // Losing either half of that (a collider left cold, or a response sneaking back into this loop) turns
  // it into two threads racing a write on m_boundingBox / the collider's support mesh.
  // Dynamic scheduling because the work per edge depends on how many colliders overlap it in x.
  {
    ECS3D_PROFILE_SCOPE("Narrow phase");

#pragma omp parallel for default(none) shared(forwardHits) schedule(dynamic, 8) num_threads(6)
    for (int i = 0; i < m_collisionEdges.size(); ++i)
    {
      findCollisions(i, forwardHits[i]);
    }
  }

  {
    ECS3D_PROFILE_SCOPE("Response");

    // Each side's own narrow-phase answer goes to that side's list. Walking i upward keeps every list in
    // ascending edge order: the entries from lower edges arrive first, then this edge's own forward hits.
    for (size_t i = 0; i < edgeCount; ++i)
    {
      for (const auto& hit : forwardHits[i])
      {
        if (hit.lowerSees)
        {
          m_touches[i].push_back(hit.other);
        }

        if (hit.higherSees)
        {
          m_touches[hit.other].push_back(i);
        }
      }
    }

    // Solved after the narrow phase's parallel region is done: the solver moves the bodies of a pair, which
    // would invalidate a collider cache another thread might still be reading if this ran inside the loop above.
    resolveInIslands(dt);

    recordCollisionEvents();
  }
}

void CollisionSystem::resolveInIslands(const float dt)
{
  const size_t edgeCount = m_collisionEdges.size();

  std::vector<size_t> parent(edgeCount);
  std::iota(parent.begin(), parent.end(), size_t{ 0 });

  const auto find = [&parent](size_t x)
  {
    while (parent[x] != x)
    {
      parent[x] = parent[parent[x]];
      x = parent[x];
    }

    return x;
  };

  // Colliders of one body, or of a body nested under another body, move together, so solving through any of
  // them writes state the others read. A static parent (a grouping folder) moves nothing and links nothing.
  std::unordered_map<const Object*, size_t> edgeOfRootBody;

  // The solver moves both bodies of a dynamic pair, so those bodies share an island. A static collider is only
  // read, so it links nothing - but its lazily built contact caches are filled here, before any thread reads it.
  for (size_t i = 0; i < edgeCount; ++i)
  {
    const auto& edge = m_collisionEdges[i];
    if (edge.body)
    {
      const Object* root = edge.body->getOwner();
      while (const auto up = root->getParent())
      {
        const auto outer = up->getComponent<RigidBody>(ComponentType::rigidBody);
        if (!outer)
        {
          break;
        }

        root = outer->getOwner();
      }

      if (const auto [entry, inserted] = edgeOfRootBody.try_emplace(root, i); !inserted)
      {
        parent[find(i)] = find(entry->second);
      }
    }

    for (const auto& hit : m_forwardHits[i])
    {
      const auto& other = m_collisionEdges[hit.other];
      if (edge.body && other.body)
      {
        parent[find(i)] = find(hit.other);
      }
      else
      {
        collisions::warmContactCaches(edge.body ? *other.collider : *edge.collider);
      }
    }
  }

  std::vector<std::vector<PairRef>> islands;
  std::vector<size_t> islandOf(edgeCount, std::numeric_limits<size_t>::max());
  for (size_t i = 0; i < edgeCount; ++i)
  {
    for (const auto& hit : m_forwardHits[i])
    {
      const auto& edge = m_collisionEdges[i];
      const auto& other = m_collisionEdges[hit.other];

      // Triggers still recorded the pair (events fire), but get no response.
      if (isTriggerPair(*edge.collider, *other.collider))
      {
        continue;
      }

      auto& slot = islandOf[find(edge.body ? i : hit.other)];
      if (slot == std::numeric_limits<size_t>::max())
      {
        slot = islands.size();
        islands.emplace_back();
      }

      // A blown-up transform gives a NaN bound, which would break the sort's ordering.
      const float bottom = std::min(edge.bounds.minY, other.bounds.minY);
      islands[slot].push_back({ i, &hit, std::isnan(bottom) ? std::numeric_limits<float>::lowest() : bottom });
    }
  }

  // Lowest first, so the solver's passes run up and down each stack rather than across it in sweep order, and
  // the order does not depend on where along x a body sits.
  for (auto& island : islands)
  {
    std::ranges::stable_sort(island, std::ranges::less{}, &PairRef::bottom);
  }

  // Largest first, so the long islands start early and the small ones fill in the gaps behind them.
  std::ranges::stable_sort(islands, std::ranges::greater{}, [](const std::vector<PairRef>& island)
  {
    return island.size();
  });

  std::exception_ptr failure;
  const int islandCount = static_cast<int>(islands.size());
  std::vector<std::vector<ContactSolver::CachedContact>> resolved(islands.size());

#pragma omp parallel default(none) shared(islands, resolved, failure, dt, islandCount) num_threads(6)
  {
    // A worker's inertia cache would otherwise carry entries from earlier ticks.
    PhysicsSystem::clearInertiaCache();

#pragma omp for schedule(dynamic)
    for (int n = 0; n < islandCount; ++n)
    {
      try
      {
        solveIsland(islands[n], resolved[n], dt);
      }
      catch (...)
      {
#pragma omp critical(collisionIslandFailure)
        {
          if (!failure)
          {
            failure = std::current_exception();
          }
        }
      }
    }
  }

  m_contactCache.clear();
  for (const auto& island : resolved)
  {
    for (const auto& contact : island)
    {
      m_contactCache.insert_or_assign(ContactSolver::PairKey{ contact.first, contact.second }, contact);
    }
  }

  if (failure)
  {
    std::rethrow_exception(failure);
  }
}

void CollisionSystem::solveIsland(const std::vector<PairRef>& island,
                                  std::vector<ContactSolver::CachedContact>& resolved, const float dt) const
{
  std::vector<ContactPair> pairs;
  pairs.reserve(island.size());

  for (const auto& ref : island)
  {
    // The narrow phase is not symmetric in its arguments, so the contact is asked for the way the sweep saw it.
    const auto& hit = *ref.hit;
    const bool lowerFirst = hit.approaching ? m_collisionEdges[ref.lower].body != nullptr : hit.lowerSees;
    const auto& first = m_collisionEdges[lowerFirst ? ref.lower : hit.other];
    const auto& second = m_collisionEdges[lowerFirst ? hit.other : ref.lower];
    const auto& witness = hit.lowerSees ? hit.lowerWitness : hit.higherWitness;

    // A pair that only touches passes the overlap test but has no depth to report, so it too is asked for as
    // the pair it becomes once this tick's motion presses it together.
    auto contact = hit.approaching ? std::nullopt
                                   : collisions::findContact(*first.collider, *second.collider, witness);
    if (hit.approaching)
    {
      for (const auto& sweep : approachSweeps(first, second))
      {
        if (!contact && sweep != glm::vec3(0))
        {
          contact = collisions::findContact(*first.collider, *second.collider, sweep);
        }
      }
    }
    if (!contact && !hit.approaching)
    {
      // A pair that only touches passes the overlap test but has no depth to measure. Pressed a little way
      // together it has, and the contact reports that press apart from the overlap, which is none.
      constexpr float press = 0.02f;
      const auto apart = second.collider->getPosition() - first.collider->getPosition();
      const float distance = glm::length(apart);
      if (distance > 0.0f)
      {
        contact = collisions::findContact(*first.collider, *second.collider, apart * (press / distance));
      }
    }

    if (contact)
    {
      pairs.push_back({ first.body.get(), second.body.get(), first.collider.get(), second.collider.get(), *contact });
    }
  }

  liftOutOfStatic(pairs);
  ContactSolver::solve(pairs, m_contactCache, resolved, dt);
}

void CollisionSystem::recordCollisionEvents()
{
  // Flatten the per-edge results into this tick's canonical pair set. A dynamic-vs-dynamic contact is
  // tested once from each side and the two answers can differ, so canonicalize (a < b) and dedupe.
  std::vector<CollisionPair> current;
  for (size_t i = 0; i < m_collisionEdges.size(); ++i)
  {
    const auto& selfUUID = m_collisionEdges[i].object->getUUID();

    for (const auto other : m_touches[i])
    {
      current.push_back(CollisionPair::make(selfUUID, m_collisionEdges[other].object->getUUID()));
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
  m_contactCache.clear();
  m_previousPairs.clear();
  m_enters.clear();
  m_stays.clear();
  m_exits.clear();
}

void CollisionSystem::findCollisions(const size_t index, std::vector<SweepHit>& hits) const
{
  // The bounds copied into m_sweepBoxes serially, before this parallel loop: reading them never touches a
  // collider, and nothing moves a transform until the response pass after the loop.
  const auto& self = m_sweepBoxes[index];
  const auto& bbox = self.bounds;
  const bool selfDynamic = self.dynamic;

  for (size_t j = index + 1; j < m_sweepBoxes.size(); ++j)
  {
    const auto& box = m_sweepBoxes[j];

    if (box.bounds.minX > bbox.maxX)
    {
      break;
    }

    // A pair of static colliders never needs testing.
    if (!selfDynamic && !box.dynamic)
    {
      continue;
    }

    const auto& otherBbox = box.bounds;

    if (bbox.maxX < otherBbox.minX || bbox.minX > otherBbox.maxX ||
        bbox.maxY < otherBbox.minY || bbox.minY > otherBbox.maxY ||
        bbox.maxZ < otherBbox.minZ || bbox.minZ > otherBbox.maxZ)
    {
      continue;
    }

    // Layer/mask filter: skip pairs that don't share a collision layer before any narrow-phase or event
    // work, so filtered layers produce neither a physical response nor a collision event.
    if (!layersCollide(self, box))
    {
      continue;
    }

    const auto& edge = m_collisionEdges[index];
    const auto& other = m_collisionEdges[j];

    if (partOfSameAssembly(edge, other))
    {
      continue;
    }

    // The narrow phase is not symmetric in its arguments, so each dynamic side asks with itself first and
    // keeps its own answer.
    SweepHit hit{ j, false, false, std::nullopt, std::nullopt };
    hit.lowerSees = selfDynamic && collisions::intersects(*edge.collider, *other.collider, hit.lowerWitness);
    hit.higherSees = box.dynamic && collisions::intersects(*other.collider, *edge.collider, hit.higherWitness);

    if (!hit.lowerSees && !hit.higherSees && !isTriggerPair(*edge.collider, *other.collider))
    {
      // Against each other as they move now, and each alone against the other standing still: a body about to
      // be stopped by its own support no longer moves with what falls onto it.
      for (const auto& sweep : approachSweeps(edge, other))
      {
        hit.approaching = hit.approaching ||
                          (sweep != glm::vec3(0) && collisions::approaches(*edge.collider, *other.collider, sweep));
      }
    }

    if (hit.lowerSees || hit.higherSees || hit.approaching)
    {
      hits.push_back(std::move(hit));
    }
  }
}

std::array<glm::vec3, 3> CollisionSystem::approachSweeps(const CollisionEdge& first, const CollisionEdge& second)
{
  return { first.sweep - second.sweep, first.sweep, -second.sweep };
}

void CollisionSystem::liftOutOfStatic(std::vector<ContactPair>& pairs)
{
  // Deeper than a resting or landing contact ever is: the body moved this far into static geometry in one tick.
  constexpr float deepOverlap = collisions::contactMargin;

  const auto isDeepInStatic = [](const ContactPair& pair)
  {
    return (pair.body == nullptr) != (pair.otherBody == nullptr) && pair.contact.overlap() > deepOverlap;
  };

  std::vector<size_t> deep;
  for (size_t i = 0; i < pairs.size(); ++i)
  {
    if (isDeepInStatic(pairs[i]))
    {
      deep.push_back(i);
    }
  }

  if (deep.empty())
  {
    return;
  }

  // Deepest first: the way out of a thin collider lying on a thick one can point further into the thick one, and
  // only the thick one's own way out is to be trusted. Each move is followed by measuring the body's other
  // contacts again, since it may have cleared them or changed their depth.
  std::ranges::stable_sort(deep, std::ranges::greater{}, [&pairs](const size_t index)
  {
    return pairs[index].contact.overlap();
  });

  std::vector<bool> gone(pairs.size(), false);
  for (const auto index : deep)
  {
    if (gone[index] || !isDeepInStatic(pairs[index]))
    {
      continue;
    }

    const auto& pair = pairs[index];
    RigidBody* body = pair.body ? pair.body : pair.otherBody;
    const auto transform = body->getOwner()->getComponent<Transform>(ComponentType::transform);
    if (!transform)
    {
      continue;
    }

    const auto away = pair.body ? pair.contact.normal() : -pair.contact.normal();
    transform->moveWorld(away * (pair.contact.overlap() - deepOverlap * 0.5f));

    for (size_t i = 0; i < pairs.size(); ++i)
    {
      if (gone[i] || (pairs[i].body != body && pairs[i].otherBody != body))
      {
        continue;
      }

      if (const auto contact = collisions::findContact(*pairs[i].collider, *pairs[i].other))
      {
        pairs[i].contact = *contact;
      }
      else
      {
        gone[i] = true;
      }
    }
  }

  size_t kept = 0;
  for (size_t i = 0; i < pairs.size(); ++i)
  {
    if (!gone[i])
    {
      pairs[kept++] = pairs[i];
    }
  }
  pairs.resize(kept);
}

bool CollisionSystem::isTriggerPair(const Collider& collider, const Collider& other)
{
  return collider.isTrigger() || other.isTrigger();
}

bool CollisionSystem::layersCollide(const SweepBox& a, const SweepBox& b)
{
  // layer is 0-31 (clamped in Collider::setLayer), so the shift stays in range.
  const uint32_t aBit = 1u << a.layer;
  const uint32_t bBit = 1u << b.layer;

  return (a.mask & bBit) != 0u && (b.mask & aBit) != 0u;
}
