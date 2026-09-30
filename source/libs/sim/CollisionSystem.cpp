#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "GeometryKey.h"
#include "Sleeping.h"
#include "collisions/NarrowPhase.h"
#include <Log.h>
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/RigidBody.h>
#include <objects/components/Transform.h>
#include <objects/components/collisions/Collider.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iomanip>
#include <iterator>
#include <limits>
#include <numeric>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace {
  constexpr uint64_t reportInterval = 250;

  // One candidate's narrow-phase result, kept beside its squared penetration depth so
  // CollisionSystem::handleCollisions can sort by depth and then resolve without recomputing the contact.
  struct ScoredContact {
    float distance;
    std::shared_ptr<Object> object;
    std::optional<collisions::Contact> contact;
    size_t index;
  };

  uint64_t microsSince(const std::chrono::steady_clock::time_point start)
  {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start).count());
  }
}

void CollisionSystem::fixedUpdate(const ObjectManager& objectManager, const float dt)
{
  const auto gatherStart = std::chrono::steady_clock::now();

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

  m_counters.gatherMicros += microsSince(gatherStart);

  checkCollisions(dt);
}

void CollisionSystem::checkCollisions(const float dt)
{
  const auto checkStart = std::chrono::steady_clock::now();
  auto stageStart = checkStart;

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

  std::vector<uint64_t> narrowPhaseCalls(m_collisionEdges.size(), 0);
  std::vector<uint64_t> contactsComputed(m_collisionEdges.size(), 0);
  std::vector<std::vector<uint32_t>> wakeRequests;
  m_cachedContacts.assign(m_collisionEdges.size(), {});
  const bool useTree = m_broadPhaseMode == BroadPhaseMode::tree;

  if (m_wakeAll)
  {
    wakeEverything();
  }

  if (useTree)
  {
    buildEdgeInfos();
  }

  m_counters.warmSortMicros += microsSince(stageStart);

  if (useTree)
  {
    updateBroadPhase();
  }

  stageStart = std::chrono::steady_clock::now();

  if (useTree)
  {
    wakeRequests.resize(m_collisionEdges.size());
    collideWithCandidates(perEdgeCollisions, narrowPhaseCalls, contactsComputed, wakeRequests);

    if (m_sleepingEnabled)
    {
      applyWakeRequests(wakeRequests);
    }
  }
  else
  {
    sweepCollisions(perEdgeCollisions, narrowPhaseCalls);
  }

  m_counters.narrowMicros += microsSince(stageStart);
  stageStart = std::chrono::steady_clock::now();

  // Applied serially, now that the parallel region is done: a response moves the transform of either
  // object in a pair, which would invalidate a collider cache another thread might still be reading if
  // this ran inside the loop above.
  for (const auto i : responseOrder(perEdgeCollisions))
  {
    // An island woken by this tick's contacts was still asleep when the contacts were found.
    if (useTree && m_edgeInfos[i].asleep)
    {
      continue;
    }

    const auto rigidBody = m_collisionEdges[i].object->getComponent<RigidBody>(ComponentType::rigidBody);
    if (!rigidBody)
    {
      continue;
    }

    handleCollisions(i, rigidBody, perEdgeCollisions[i], dt);
  }

  m_counters.responseMicros += microsSince(stageStart);
  stageStart = std::chrono::steady_clock::now();

  recordCollisionEvents(perEdgeCollisions);

  if (useTree && m_sleepingEnabled)
  {
    updateSleeping(perEdgeCollisions);
  }

  m_counters.eventsMicros += microsSince(stageStart);

  ++m_counters.ticks;
  m_counters.narrowPhaseCalls += std::accumulate(narrowPhaseCalls.begin(), narrowPhaseCalls.end(), uint64_t{0});
  m_counters.contactsComputed += std::accumulate(contactsComputed.begin(), contactsComputed.end(), uint64_t{0});
  m_counters.checkMicros += microsSince(checkStart);

  if (m_counters.ticks >= reportInterval)
  {
    reportCounters();
  }
}

void CollisionSystem::sweepCollisions(std::vector<std::vector<std::shared_ptr<Object>>>& perEdgeCollisions,
                                      std::vector<uint64_t>& narrowPhaseCalls)
{
  // This loop has to stay read-only: findCollisions reads bounding boxes and (through the narrow phase)
  // transformed meshes that live on the same Collider another thread's iteration can also read. That is
  // only safe because every collider in m_collisionEdges was just warmed serially above, and nothing in
  // this loop moves a transform - collision responses, which do, are deferred to the serial pass below.
  // Losing either half of that (a collider left cold, or a response sneaking back into this loop) turns
  // it into two threads racing a write on m_boundingBox / m_transformedBoxVertices.
#pragma omp parallel for default(none) shared(perEdgeCollisions, narrowPhaseCalls) num_threads(6)
  for (int i = 0; i < m_collisionEdges.size(); ++i)
  {
    const auto& edge = m_collisionEdges[i];

    if (!edge.object->getComponent<RigidBody>(ComponentType::rigidBody))
    {
      continue;
    }

    std::vector<std::shared_ptr<Object>> collidedObjects;
    narrowPhaseCalls[i] = findCollisions(edge, collidedObjects);

    if (!collidedObjects.empty())
    {
      perEdgeCollisions[i] = std::move(collidedObjects);
    }
  }
}

void CollisionSystem::buildEdgeInfos()
{
  m_edgeInfos.clear();
  m_edgeInfos.reserve(m_collisionEdges.size());

  for (const auto& edge : m_collisionEdges)
  {
    EdgeInfo info;
    info.object = edge.object.get();
    info.parent = edge.object->getParent().get();
    info.collider = edge.collider.get();
    const auto body = edge.object->getComponent<RigidBody>(ComponentType::rigidBody);
    info.hasRigidBody = static_cast<bool>(body);
    info.layer = edge.collider->getLayer();
    info.mask = edge.collider->getMask();
    info.box = edge.collider->cachedBoundingBox();
    info.trigger = edge.collider->isTrigger();
    info.geometryKey = geometryKeyOf(*edge.object);

    if (m_sleepingEnabled && body)
    {
      info.body = body.get();
      info.asleep = body->isAsleep();
      info.islandId = body->getIslandId();
    }

    m_edgeInfos.push_back(info);
  }

  if (!m_sleepingEnabled)
  {
    return;
  }

  m_bodies.clear();
  m_edgeOfObject.clear();
  m_edgeOfObject.reserve(m_edgeInfos.size());

  for (size_t i = 0; i < m_edgeInfos.size(); ++i)
  {
    m_edgeOfObject.emplace(m_edgeInfos[i].object, static_cast<int32_t>(i));

    if (m_edgeInfos[i].body)
    {
      m_bodies.push_back(m_edgeInfos[i].body);

      if (m_edgeInfos[i].asleep)
      {
        ++m_counters.sleepSkippedEdges;
      }
    }
  }

  std::ranges::sort(m_bodies);
  m_bodies.erase(std::unique(m_bodies.begin(), m_bodies.end()), m_bodies.end());

  for (auto& info : m_edgeInfos)
  {
    if (info.body)
    {
      info.bodyIndex = static_cast<size_t>(std::ranges::lower_bound(m_bodies, info.body) - m_bodies.begin());
    }
  }

  const auto asleepNow = static_cast<uint64_t>(std::ranges::count_if(m_bodies, [](const RigidBody* body)
  {
    return body->isAsleep();
  }));

  if (m_asleepBodiesLastTick > asleepNow)
  {
    m_counters.woke += m_asleepBodiesLastTick - asleepNow;
  }
}

void CollisionSystem::updateBroadPhase()
{
  const auto start = std::chrono::steady_clock::now();

  const auto isFinite = [](const BoundingBox& box)
  {
    return std::isfinite(box.minX) && std::isfinite(box.maxX) && std::isfinite(box.minY) &&
           std::isfinite(box.maxY) && std::isfinite(box.minZ) && std::isfinite(box.maxZ);
  };

  // An edge with a non-finite box is left out entirely and so collides with nothing this tick; the sweep
  // would still test it, which is a deliberate divergence since a tree cannot hold a NaN box.
  std::vector<BroadPhaseInput> inputs;
  inputs.reserve(m_collisionEdges.size());

  for (size_t i = 0; i < m_collisionEdges.size(); ++i)
  {
    const auto& edge = m_collisionEdges[i];
    const auto& info = m_edgeInfos[i];
    const auto& box = info.box;
    if (!isFinite(box))
    {
      continue;
    }

    BroadPhaseInput input;
    input.key = edge.collider.get();
    input.collider = edge.collider;
    input.tight = { glm::vec3(box.minX, box.minY, box.minZ), glm::vec3(box.maxX, box.maxY, box.maxZ) };
    input.edgeIndex = static_cast<int32_t>(i);

    if (info.hasRigidBody)
    {
      const auto body = edge.object->getComponent<RigidBody>(ComponentType::rigidBody);
      input.dynamic = true;

      // Velocity is displacement per tick.
      const auto velocity = body->getVelocity();
      if (std::isfinite(velocity.x) && std::isfinite(velocity.y) && std::isfinite(velocity.z))
      {
        input.displacement = velocity;
      }
    }

    inputs.push_back(std::move(input));
  }

  m_broadPhase.update(inputs);

  m_candidates.assign(m_collisionEdges.size(), {});
  for (const auto pair : m_broadPhase.getPairs())
  {
    const auto a = m_broadPhase.edgeIndexOf(BroadPhase::firstOf(pair));
    const auto b = m_broadPhase.edgeIndexOf(BroadPhase::secondOf(pair));

    if (m_edgeInfos[static_cast<size_t>(a)].hasRigidBody)
    {
      m_candidates[static_cast<size_t>(a)].push_back(b);
    }

    if (m_edgeInfos[static_cast<size_t>(b)].hasRigidBody)
    {
      m_candidates[static_cast<size_t>(b)].push_back(a);
    }
  }

  for (auto& list : m_candidates)
  {
    std::ranges::sort(list);
    m_counters.candidates += list.size();
  }

  m_counters.reinserts += m_broadPhase.getStats().reinserts;
  m_counters.broadPhaseMicros += static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::steady_clock::now() - start).count());
}

void CollisionSystem::collideWithCandidates(std::vector<std::vector<std::shared_ptr<Object>>>& perEdgeCollisions,
                                            std::vector<uint64_t>& narrowPhaseCalls,
                                            std::vector<uint64_t>& contactsComputed,
                                            std::vector<std::vector<uint32_t>>& wakeRequests)
{
  // Read-only for the same reason as sweepCollisions: every box was warmed serially and responses wait. A wake
  // request goes into the requesting edge's own list, so threads still write only their own slots.
#pragma omp parallel for default(none) shared(perEdgeCollisions, narrowPhaseCalls, contactsComputed, wakeRequests) num_threads(6) schedule(dynamic, 16)
  for (int i = 0; i < m_collisionEdges.size(); ++i)
  {
    if (m_edgeInfos[i].asleep)
    {
      std::vector<std::shared_ptr<Object>> replayed;
      collectSleepingHits(static_cast<size_t>(i), replayed, wakeRequests[i]);

      if (!replayed.empty())
      {
        perEdgeCollisions[i] = std::move(replayed);
      }

      continue;
    }

    if (m_candidates[i].empty())
    {
      continue;
    }

    std::vector<std::shared_ptr<Object>> collidedObjects;
    std::vector<CachedContact> cachedContacts;
    const auto stats = findCollisionsFromCandidates(static_cast<size_t>(i), m_candidates[i], collidedObjects,
                                                    cachedContacts, wakeRequests[i]);
    narrowPhaseCalls[i] = stats.narrowPhaseCalls;
    contactsComputed[i] = stats.contactsComputed;

    if (!collidedObjects.empty())
    {
      perEdgeCollisions[i] = std::move(collidedObjects);
      m_cachedContacts[i] = std::move(cachedContacts);
    }
  }
}

void CollisionSystem::reportCounters()
{
  const auto ticks = static_cast<double>(m_counters.ticks);
  const auto& stats = m_broadPhase.getStats();
  const bool tree = m_broadPhaseMode == BroadPhaseMode::tree;

  std::ostringstream message;
  message << std::fixed << std::setprecision(1)
          << "broadphase=" << (tree ? "tree" : "sweep") << " ticks=" << m_counters.ticks
          << " candidates/tick=" << static_cast<double>(m_counters.candidates) / ticks
          << " narrow/tick=" << static_cast<double>(m_counters.narrowPhaseCalls) / ticks
          << " contacts_parallel/tick=" << static_cast<double>(m_counters.contactsComputed) / ticks
          << " contacts_reused/tick=" << static_cast<double>(m_counters.contactsReused) / ticks
          << " contacts_recomputed/tick=" << static_cast<double>(m_counters.contactsRecomputed) / ticks
          << " reinserts/tick=" << static_cast<double>(m_counters.reinserts) / ticks
          << " gather_us/tick=" << static_cast<double>(m_counters.gatherMicros) / ticks
          << " warm_sort_us/tick=" << static_cast<double>(m_counters.warmSortMicros) / ticks
          << " broadphase_us/tick=" << static_cast<double>(m_counters.broadPhaseMicros) / ticks
          << " narrow_us/tick=" << static_cast<double>(m_counters.narrowMicros) / ticks
          << " response_us/tick=" << static_cast<double>(m_counters.responseMicros) / ticks
          << " events_us/tick=" << static_cast<double>(m_counters.eventsMicros) / ticks
          << " check_us/tick=" << static_cast<double>(m_counters.checkMicros) / ticks;

  if (tree)
  {
    message << " pairs=" << stats.pairCount << " static=" << stats.staticProxies
            << " dynamic=" << stats.dynamicProxies << " heights=" << stats.staticHeight << "/"
            << stats.dynamicHeight;
  }

  if (tree && m_sleepingEnabled)
  {
    message << " asleep_bodies=" << m_counters.bodiesAsleep << " asleep_islands=" << m_counters.islandsAsleep
            << " fell_asleep/tick=" << static_cast<double>(m_counters.fellAsleep) / ticks
            << " woke/tick=" << static_cast<double>(m_counters.woke) / ticks
            << " sleep_skipped_edges/tick=" << static_cast<double>(m_counters.sleepSkippedEdges) / ticks;
  }

  pruneSleepRecords();

  Log::info(LogCategory::physics, message.str());

  m_counters = {};
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

void CollisionSystem::setBroadPhaseMode(const BroadPhaseMode mode)
{
  if (mode == m_broadPhaseMode)
  {
    return;
  }

  m_broadPhaseMode = mode;
  m_broadPhase.clear();
  m_candidates.clear();
  m_counters = {};
  clearSleepState();
}

void CollisionSystem::setSleepingEnabled(const bool enabled)
{
  if (m_sleepingEnabled && !enabled)
  {
    clearSleepState();
  }

  m_sleepingEnabled = enabled;
}

void CollisionSystem::clearSleepState()
{
  m_sleepRecords.clear();
  m_nextIslandId = 1;
  m_asleepBodiesLastTick = 0;
  m_wakeAll = true;
}

void CollisionSystem::reset()
{
  m_broadPhase.clear();
  m_candidates.clear();
  m_previousPairs.clear();
  m_enters.clear();
  m_stays.clear();
  m_exits.clear();
  clearSleepState();
}

uint64_t CollisionSystem::findCollisions(const CollisionEdge& edge,
                                         std::vector<std::shared_ptr<Object>>& collidedObjects) const
{
  // cachedBoundingBox(), not getBoundingBox(): this runs inside checkCollisions' parallel loop, where
  // every collider's bounding box was already warmed serially and nothing moves a transform until the
  // serial response pass after the loop. getBoundingBox() would recompute (and write) on a cache miss;
  // cachedBoundingBox() only ever reads, which is what makes concurrent calls on a shared collider safe.
  const auto& bbox = edge.collider->cachedBoundingBox();
  uint64_t narrowPhaseCalls = 0;

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

    ++narrowPhaseCalls;
    if (collisions::intersects(*edge.collider, *other.collider))
    {
      collidedObjects.emplace_back(other.object);
    }
  }

  return narrowPhaseCalls;
}

CollisionSystem::CandidateStats CollisionSystem::findCollisionsFromCandidates(
  const size_t edgeIndex, const std::vector<int32_t>& candidates,
  std::vector<std::shared_ptr<Object>>& collidedObjects, std::vector<CachedContact>& cachedContacts,
  std::vector<uint32_t>& wakeIslands) const
{
  const auto& self = m_edgeInfos[edgeIndex];
  const auto& bbox = self.box;
  CandidateStats stats;

  // Touching a sleeper wakes its island, unless the touch is a trigger overlap, which has no response.
  const auto wakeIfAsleep = [&self, &wakeIslands](const EdgeInfo& touched)
  {
    if (touched.asleep && !self.trigger && !touched.trigger)
    {
      wakeIslands.push_back(touched.islandId);
    }
  };

  for (const auto index : candidates)
  {
    const auto& other = m_edgeInfos[static_cast<size_t>(index)];

    if (other.object == self.object ||
        other.parent == self.object ||
        other.object == self.parent)
    {
      continue;
    }

    // layer is 0-31, so the shift stays in range; same test as layersCollide.
    const uint32_t selfBit = 1u << self.layer;
    const uint32_t otherBit = 1u << other.layer;
    if (!((self.mask & otherBit) != 0u && (other.mask & selfBit) != 0u))
    {
      continue;
    }

    const auto& otherBbox = other.box;

    if (bbox.maxX < otherBbox.minX || bbox.minX > otherBbox.maxX ||
        bbox.maxY < otherBbox.minY || bbox.minY > otherBbox.maxY ||
        bbox.maxZ < otherBbox.minZ || bbox.minZ > otherBbox.maxZ)
    {
      continue;
    }

    ++stats.narrowPhaseCalls;

    // A trigger never uses a contact, so it only needs the overlap test.
    if (!m_contactCacheEnabled || self.trigger || other.trigger)
    {
      if (collisions::intersects(*self.collider, *other.collider))
      {
        collidedObjects.emplace_back(m_collisionEdges[static_cast<size_t>(index)].object);
        wakeIfAsleep(other);

        if (m_contactCacheEnabled)
        {
          cachedContacts.push_back({ false, std::nullopt, self.geometryKey, other.geometryKey });
        }
      }

      continue;
    }

    auto result = collisions::collide(*self.collider, *other.collider);
    ++stats.contactsComputed;

    if (result.intersects)
    {
      collidedObjects.emplace_back(m_collisionEdges[static_cast<size_t>(index)].object);
      wakeIfAsleep(other);
      cachedContacts.push_back({ true, std::move(result.contact), self.geometryKey, other.geometryKey });
    }
  }

  return stats;
}

void CollisionSystem::handleCollisions(const size_t edgeIndex, const std::shared_ptr<RigidBody>& rigidBody,
                                       const std::vector<std::shared_ptr<Object>>& collidedObjects, const float dt)
{
  const auto& collider = m_collisionEdges[edgeIndex].collider;

  if (collidedObjects.size() == 1)
  {
    // Triggers still recorded the pair (events fire), but get no MTV correction / impulse.
    if (isTriggerPair(collider, collidedObjects[0]))
    {
      return;
    }

    if (const auto contact = contactFor(edgeIndex, 0, collidedObjects[0]))
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

  for (size_t k = 0; k < collidedObjects.size(); ++k)
  {
    const auto& collidedObject = collidedObjects[k];
    auto contact = contactFor(edgeIndex, k, collidedObject);
    const float distance = contact ? dot(contact->minimumTranslationVector, contact->minimumTranslationVector)
                                   : 0.0f;

    scoredContacts.push_back({ distance, collidedObject, std::move(contact), k });
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
    const auto contact = bodyMoved ? contactFor(edgeIndex, scoredContact.index, scoredContact.object)
                                   : scoredContact.contact;
    if (!contact)
    {
      continue;
    }

    PhysicsSystem::handleCollision(*rigidBody, scoredContact.object, contact->minimumTranslationVector,
                                   contact->contactPoints(), dt);
    bodyMoved = true;
  }
}

std::optional<collisions::Contact> CollisionSystem::contactFor(const size_t edgeIndex, const size_t k,
                                                               const std::shared_ptr<Object>& other)
{
  const auto& edge = m_collisionEdges[edgeIndex];
  const auto& cached = m_cachedContacts[edgeIndex];

  if (k < cached.size())
  {
    const auto& entry = cached[k];

    if (entry.computed && entry.selfKey == geometryKeyOf(*edge.object) &&
        entry.otherKey == geometryKeyOf(*other))
    {
      ++m_counters.contactsReused;
      return entry.contact;
    }

    ++m_counters.contactsRecomputed;
  }

  return contactWith(edge.collider, other);
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

void CollisionSystem::collectSleepingHits(const size_t edgeIndex,
                                          std::vector<std::shared_ptr<Object>>& collidedObjects,
                                          std::vector<uint32_t>& wakeIslands) const
{
  const auto& self = m_edgeInfos[edgeIndex];

  const auto found = m_sleepRecords.find(self.collider);
  if (found == m_sleepRecords.end() || found->second.collider.lock().get() != self.collider)
  {
    wakeIslands.push_back(self.islandId);
    return;
  }

  const auto& record = found->second;
  bool stale = record.selfKey != self.geometryKey;

  for (const auto& hit : record.hits)
  {
    auto object = hit.object.lock();
    if (!object)
    {
      stale = true;
      continue;
    }

    const auto edge = m_edgeOfObject.find(object.get());
    if (edge == m_edgeOfObject.end())
    {
      stale = true;
      continue;
    }

    const auto& other = m_edgeInfos[static_cast<size_t>(edge->second)];
    if (other.geometryKey != hit.key || (hit.dynamic && !hit.trigger && other.body && !other.asleep))
    {
      stale = true;
    }

    // Reported either way, so the pair keeps producing stay events instead of a spurious exit.
    collidedObjects.push_back(std::move(object));
  }

  if (stale)
  {
    wakeIslands.push_back(self.islandId);
  }
}

void CollisionSystem::applyWakeRequests(const std::vector<std::vector<uint32_t>>& requests)
{
  std::vector<uint32_t> islands;
  for (const auto& request : requests)
  {
    islands.insert(islands.end(), request.begin(), request.end());
  }

  std::ranges::sort(islands);
  islands.erase(std::unique(islands.begin(), islands.end()), islands.end());

  if (!islands.empty() && islands.front() == 0)
  {
    islands.erase(islands.begin());
  }

  if (islands.empty())
  {
    return;
  }

  for (auto* body : m_bodies)
  {
    if (body->isAsleep() && std::ranges::binary_search(islands, body->getIslandId()))
    {
      body->setAsleep(false);
      body->setRestTicks(0);
      ++m_counters.woke;
    }
  }

  std::erase_if(m_sleepRecords, [&islands](const auto& entry)
  {
    return std::ranges::binary_search(islands, entry.second.islandId);
  });
}

void CollisionSystem::wakeEverything()
{
  for (const auto& edge : m_collisionEdges)
  {
    if (const auto body = edge.object->getComponent<RigidBody>(ComponentType::rigidBody))
    {
      body->setAsleep(false);
      body->setRestTicks(0);
    }
  }

  m_sleepRecords.clear();
  m_wakeAll = false;
}

void CollisionSystem::updateSleeping(const std::vector<std::vector<std::shared_ptr<Object>>>& perEdgeCollisions)
{
  const size_t bodyCount = m_bodies.size();

  for (auto* body : m_bodies)
  {
    if (body->isAsleep())
    {
      continue;
    }

    const bool resting = !body->getNextFalling() &&
                         glm::length(body->getVelocity()) < sleeping::linearSleepSpeed &&
                         glm::length(body->getAngularVelocity()) < sleeping::angularSleepSpeed &&
                         body->getPendingForces().empty();

    body->setRestTicks(resting ? body->getRestTicks() + 1 : 0);
  }

  std::vector<size_t> parent(bodyCount);
  std::iota(parent.begin(), parent.end(), size_t{0});

  const auto root = [&parent](size_t index)
  {
    while (parent[index] != index)
    {
      parent[index] = parent[parent[index]];
      index = parent[index];
    }

    return index;
  };

  // Static contacts and trigger overlaps do not tie bodies together.
  for (size_t i = 0; i < perEdgeCollisions.size(); ++i)
  {
    const auto& self = m_edgeInfos[i];
    if (!self.body || self.body->isAsleep() || self.trigger)
    {
      continue;
    }

    for (const auto& hit : perEdgeCollisions[i])
    {
      const auto edge = m_edgeOfObject.find(hit.get());
      if (edge == m_edgeOfObject.end())
      {
        continue;
      }

      const auto& other = m_edgeInfos[static_cast<size_t>(edge->second)];
      if (!other.body || other.body->isAsleep() || other.trigger)
      {
        continue;
      }

      parent[root(self.bodyIndex)] = root(other.bodyIndex);
    }
  }

  std::vector<uint8_t> rested(bodyCount, 1);
  for (size_t b = 0; b < bodyCount; ++b)
  {
    if (!m_bodies[b]->isAsleep() && m_bodies[b]->getRestTicks() < sleeping::ticksToSleep)
    {
      rested[root(b)] = 0;
    }
  }

  std::vector<uint32_t> islandOfRoot(bodyCount, 0);
  std::vector<uint8_t> slept(bodyCount, 0);

  for (size_t b = 0; b < bodyCount; ++b)
  {
    auto& body = *m_bodies[b];
    const auto r = root(b);

    if (body.isAsleep() || !rested[r])
    {
      continue;
    }

    if (islandOfRoot[r] == 0)
    {
      islandOfRoot[r] = m_nextIslandId++;
      if (m_nextIslandId == 0)
      {
        m_nextIslandId = 1;
      }
    }

    body.setAsleep(true);
    body.setIslandId(islandOfRoot[r]);
    body.setVelocity(glm::vec3(0));
    body.setAngularVelocity(glm::vec3(0));
    body.setFalling(false);
    body.setNextFalling(false);
    body.setSleepGeometryKey(body.getOwner() ? geometryKeyOf(*body.getOwner()) : 0);
    slept[b] = 1;
    ++m_counters.fellAsleep;
  }

  // Keys are read now, after every response has moved what it is going to move this tick.
  for (size_t i = 0; i < m_edgeInfos.size(); ++i)
  {
    const auto& self = m_edgeInfos[i];
    if (!self.body || !slept[self.bodyIndex])
    {
      continue;
    }

    SleepRecord record;
    record.collider = m_collisionEdges[i].collider;
    record.islandId = islandOfRoot[root(self.bodyIndex)];
    record.selfKey = geometryKeyOf(*self.object);

    for (const auto& hit : perEdgeCollisions[i])
    {
      const auto edge = m_edgeOfObject.find(hit.get());
      if (edge == m_edgeOfObject.end())
      {
        continue;
      }

      const auto& other = m_edgeInfos[static_cast<size_t>(edge->second)];
      record.hits.push_back({ hit, geometryKeyOf(*hit), other.hasRigidBody, self.trigger || other.trigger });
    }

    m_sleepRecords[self.collider] = std::move(record);
  }

  uint64_t asleepBodies = 0;
  std::vector<uint32_t> asleepIslands;
  for (const auto* body : m_bodies)
  {
    if (body->isAsleep())
    {
      ++asleepBodies;
      asleepIslands.push_back(body->getIslandId());
    }
  }

  std::ranges::sort(asleepIslands);
  asleepIslands.erase(std::unique(asleepIslands.begin(), asleepIslands.end()), asleepIslands.end());

  m_counters.bodiesAsleep = asleepBodies;
  m_counters.islandsAsleep = asleepIslands.size();
  m_asleepBodiesLastTick = asleepBodies;
}

void CollisionSystem::pruneSleepRecords()
{
  std::erase_if(m_sleepRecords, [](const auto& entry)
  {
    return entry.second.collider.expired();
  });
}
