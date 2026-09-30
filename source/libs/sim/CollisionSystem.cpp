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
#include <array>
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

  // Untuned: how far a cached contact may drift sideways, or move along its normal, before it is measured
  // again instead of carried along.
  constexpr float refreshMaxTangentialDrift = 0.05f;
  constexpr float refreshMaxNormalMotion = 0.25f;

  ColliderPose poseOf(Collider& collider)
  {
    return { collider.getPosition(), collider.getRotation(), collider.getScale() };
  }

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

  // Adds the time between its construction and destruction to a counter.
  class ScopedMicros {
  public:
    explicit ScopedMicros(uint64_t& sink)
      : m_sink(sink),
        m_start(std::chrono::steady_clock::now())
    {}

    ~ScopedMicros()
    {
      m_sink += microsSince(m_start);
    }

    ScopedMicros(const ScopedMicros&) = delete;
    ScopedMicros& operator=(const ScopedMicros&) = delete;

  private:
    uint64_t& m_sink;
    std::chrono::steady_clock::time_point m_start;
  };

  // Union-find over rigid bodies, indexed in the order they are first seen.
  class BodyPartition {
  public:
    explicit BodyPartition(const size_t expected)
    {
      m_index.reserve(expected);
      m_bodies.reserve(expected);
      m_parent.reserve(expected);
    }

    size_t indexOf(RigidBody* body)
    {
      const auto [it, inserted] = m_index.try_emplace(body, m_parent.size());
      if (inserted)
      {
        m_bodies.push_back(body);
        m_parent.push_back(it->second);
      }

      return it->second;
    }

    size_t find(size_t index)
    {
      while (m_parent[index] != index)
      {
        m_parent[index] = m_parent[m_parent[index]];
        index = m_parent[index];
      }

      return index;
    }

    void join(const size_t a, const size_t b)
    {
      m_parent[find(a)] = find(b);
    }

    [[nodiscard]] size_t size() const { return m_parent.size(); }

    [[nodiscard]] RigidBody* bodyAt(const size_t index) const { return m_bodies[index]; }

  private:
    std::unordered_map<RigidBody*, size_t> m_index;
    std::vector<RigidBody*> m_bodies;
    std::vector<size_t> m_parent;
  };

  // How many of the ascending thresholds the value has reached.
  template <size_t N>
  size_t bucketOf(const float value, const std::array<float, N>& thresholds)
  {
    size_t bucket = 0;
    while (bucket < N && value >= thresholds[bucket])
    {
      ++bucket;
    }

    return bucket;
  }

  constexpr std::array<float, 5> speedBuckets = { 0.001f, 0.004f, 0.01f, 0.02f, 0.05f };
  constexpr std::array<float, 4> spinBuckets = { 1.0f, 3.0f, 10.0f, 30.0f };
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

  const bool useTree = m_broadPhaseMode == BroadPhaseMode::tree;

  if (useTree)
  {
    // Each edge is its own collider, and getBoundingBox writes only that collider's caches (its bounding box,
    // its transform pointer, a box's transformed mesh) while reading Transforms, so iterations do not touch
    // each other's state. Nothing writes a Transform during the loop.
    const int edgeCount = static_cast<int>(m_collisionEdges.size());
    const int threads = m_threadCount;

#pragma omp parallel for num_threads(threads) schedule(dynamic, 32)
    for (int i = 0; i < edgeCount; ++i)
    {
      auto& edge = m_collisionEdges[static_cast<size_t>(i)];
      edge.position = edge.collider->getBoundingBox().minX;
    }
  }
  else
  {
    for (auto& edge : m_collisionEdges)
    {
      edge.position = edge.collider->getBoundingBox().minX;
    }
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

  const bool parallelResponse = useTree && m_parallelResponseEnabled;
  const bool diagnostics = useTree && m_diagnosticsEnabled;

  HitBodies hitBodies;
  if (parallelResponse || diagnostics)
  {
    const auto hitGraphStart = std::chrono::steady_clock::now();
    hitBodies = collectHitBodies(perEdgeCollisions);
    m_counters.hitGraphMicros += microsSince(hitGraphStart);
  }

  if (diagnostics)
  {
    const ScopedMicros timer(m_counters.diagMicros);
    recordHitIslands(perEdgeCollisions, hitBodies);
  }

  stageStart = std::chrono::steady_clock::now();

  // Applied after the parallel region is done: a response moves the transform of either object in a pair,
  // which would invalidate a collider cache another thread might still be reading if this ran inside the
  // loop above.
  ResponseCounters response;
  const auto order = responseOrder(perEdgeCollisions);

  if (parallelResponse)
  {
    respondInParallel(order, perEdgeCollisions, hitBodies, dt, response);
  }
  else
  {
    for (const auto i : order)
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

      handleCollisions(i, rigidBody, perEdgeCollisions[i], dt, response);
    }
  }

  m_counters.response.add(response);
  m_contactsRefreshedTotal += response.contactsRefreshed;
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
  m_edgeInfos.assign(m_collisionEdges.size(), EdgeInfo{});

  // Every iteration reads its own edge (the warm above finished, and nothing writes a Transform here) and
  // writes only its own slot.
  const int edgeCount = static_cast<int>(m_collisionEdges.size());
  const int threads = m_threadCount;

#pragma omp parallel for num_threads(threads) schedule(dynamic, 32)
  for (int i = 0; i < edgeCount; ++i)
  {
    const auto& edge = m_collisionEdges[static_cast<size_t>(i)];
    EdgeInfo& info = m_edgeInfos[static_cast<size_t>(i)];
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
    info.body = body.get();

    if (m_sleepingEnabled && body)
    {
      info.asleep = body->isAsleep();
      info.islandId = body->getIslandId();
    }
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
  // Read-only for the same reason as sweepCollisions: every box was warmed before it and responses wait. A wake
  // request goes into the requesting edge's own list, so threads still write only their own slots.
  const int threads = m_threadCount;

#pragma omp parallel for default(none) shared(perEdgeCollisions, narrowPhaseCalls, contactsComputed, wakeRequests) num_threads(threads) schedule(dynamic, 16)
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
  const auto perTick = [ticks](const uint64_t value) { return static_cast<double>(value) / ticks; };
  const auto& stats = m_broadPhase.getStats();
  const auto& resp = m_counters.response;
  const bool tree = m_broadPhaseMode == BroadPhaseMode::tree;

  std::ostringstream message;
  message << std::fixed << std::setprecision(1)
          << "broadphase=" << (tree ? "tree" : "sweep") << " ticks=" << m_counters.ticks
          << " threads=" << m_threadCount
          << " candidates/tick=" << perTick(m_counters.candidates)
          << " narrow/tick=" << perTick(m_counters.narrowPhaseCalls)
          << " contacts_parallel/tick=" << perTick(m_counters.contactsComputed)
          << " contacts_reused/tick=" << perTick(resp.contactsReused)
          << " contacts_recomputed/tick=" << perTick(resp.contactsRecomputed)
          << " contacts_refreshed/tick=" << perTick(resp.contactsRefreshed)
          << " refresh_fallback_rot/drift/normal/sphere="
          << perTick(resp.refreshFellBackRotation) << "/"
          << perTick(resp.refreshFellBackDrift) << "/"
          << perTick(resp.refreshFellBackNormalMotion) << "/"
          << perTick(resp.refreshFellBackSphere)
          << " reinserts/tick=" << perTick(m_counters.reinserts)
          << " gather_us/tick=" << perTick(m_counters.gatherMicros)
          << " warm_sort_us/tick=" << perTick(m_counters.warmSortMicros)
          << " broadphase_us/tick=" << perTick(m_counters.broadPhaseMicros)
          << " narrow_us/tick=" << perTick(m_counters.narrowMicros)
          << " response_us/tick=" << perTick(m_counters.responseMicros)
          << " resp_recompute_us/tick=" << perTick(resp.recomputeMicros)
          << " resp_refresh_us/tick=" << perTick(resp.refreshMicros)
          << " resp_physics_us/tick=" << perTick(resp.physicsMicros)
          << " events_us/tick=" << perTick(m_counters.eventsMicros)
          << " check_us/tick=" << perTick(m_counters.checkMicros);

  if (tree)
  {
    message << " parallel_response=" << (m_parallelResponseEnabled ? "on" : "off")
            << " hit_graph_us/tick=" << perTick(m_counters.hitGraphMicros)
            << " diag_us/tick=" << perTick(m_counters.diagMicros);

    if (m_parallelResponseEnabled)
    {
      message << " resp_setup_us/tick=" << perTick(m_counters.responseSetupMicros)
              << " resp_components/tick=" << perTick(m_counters.responseComponents)
              << " resp_largest_component_edges/tick=" << perTick(m_counters.responseLargestComponent);
    }

    message << " pairs=" << stats.pairCount << " static=" << stats.staticProxies
            << " dynamic=" << stats.dynamicProxies << " heights=" << stats.staticHeight << "/"
            << stats.dynamicHeight;

    if (m_diagnosticsEnabled)
    {
      message << " islands/tick=" << perTick(m_counters.hitIslands)
              << " largest_island/tick=" << perTick(m_counters.largestHitIsland)
              << " largest_island_max=" << m_counters.largestHitIslandMax
              << " bodies_in_islands_ge_64/tick=" << perTick(m_counters.bodiesInLargeIslands);
    }
  }

  if (tree && m_sleepingEnabled)
  {
    message << " asleep_bodies=" << m_counters.bodiesAsleep << " asleep_islands=" << m_counters.islandsAsleep
            << " fell_asleep/tick=" << perTick(m_counters.fellAsleep)
            << " woke/tick=" << perTick(m_counters.woke)
            << " sleep_skipped_edges/tick=" << perTick(m_counters.sleepSkippedEdges);

    if (m_diagnosticsEnabled)
    {
      message << " awake/tick=" << perTick(m_counters.awakeBodies)
              << " no_sleep_unsupported/fast/spinning/forces/island_blocked="
              << perTick(m_counters.blockedUnsupported) << "/" << perTick(m_counters.blockedFast) << "/"
              << perTick(m_counters.blockedSpinning) << "/" << perTick(m_counters.blockedForces) << "/"
              << perTick(m_counters.blockedIsland)
              << " supported_speed_hist(<.001/<.004/<.01/<.02/<.05/>=.05)=";

      for (size_t i = 0; i < m_counters.supportedSpeedHistogram.size(); ++i)
      {
        message << (i ? "/" : "") << perTick(m_counters.supportedSpeedHistogram[i]);
      }

      message << " supported_spin_hist(<1/<3/<10/<30/>=30)=";

      for (size_t i = 0; i < m_counters.supportedSpinHistogram.size(); ++i)
      {
        message << (i ? "/" : "") << perTick(m_counters.supportedSpinHistogram[i]);
      }
    }
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

CollisionSystem::HitBodies CollisionSystem::collectHitBodies(
  const std::vector<std::vector<std::shared_ptr<Object>>>& perEdgeCollisions) const
{
  HitBodies result;
  result.offsets.assign(perEdgeCollisions.size() + 1, 0);

  for (size_t i = 0; i < perEdgeCollisions.size(); ++i)
  {
    result.offsets[i + 1] = result.offsets[i] + perEdgeCollisions[i].size();
  }

  result.flat.resize(result.offsets.back());

  // Reads component maps only; every iteration writes its own span of the flat array.
  const int edgeCount = static_cast<int>(perEdgeCollisions.size());
  const int threads = m_threadCount;

#pragma omp parallel for num_threads(threads) schedule(dynamic, 32)
  for (int i = 0; i < edgeCount; ++i)
  {
    const auto& hits = perEdgeCollisions[static_cast<size_t>(i)];
    HitBody* out = result.flat.data() + result.offsets[static_cast<size_t>(i)];

    for (size_t k = 0; k < hits.size(); ++k)
    {
      const auto body = hits[k]->getComponent<RigidBody>(ComponentType::rigidBody);
      const auto collider = hits[k]->getComponent<Collider>(ComponentType::collider);

      out[k].body = body.get();
      out[k].trigger = collider && collider->isTrigger();
    }
  }

  return result;
}

void CollisionSystem::recordHitIslands(const std::vector<std::vector<std::shared_ptr<Object>>>& perEdgeCollisions,
                                       const HitBodies& hitBodies)
{
  BodyPartition partition(perEdgeCollisions.size());

  for (size_t i = 0; i < perEdgeCollisions.size(); ++i)
  {
    const auto& self = m_edgeInfos[i];
    if (self.asleep || self.trigger || !self.body || perEdgeCollisions[i].empty())
    {
      continue;
    }

    bool registered = false;
    size_t selfIndex = 0;

    for (size_t k = 0; k < perEdgeCollisions[i].size(); ++k)
    {
      const auto& hit = hitBodies.flat[hitBodies.offsets[i] + k];
      if (hit.trigger)
      {
        continue;
      }

      if (!registered)
      {
        selfIndex = partition.indexOf(self.body);
        registered = true;
      }

      // Static contacts do not tie bodies together.
      if (hit.body)
      {
        const auto otherIndex = partition.indexOf(hit.body);
        partition.join(selfIndex, otherIndex);
      }
    }
  }

  std::vector<uint64_t> sizes(partition.size(), 0);
  for (size_t b = 0; b < partition.size(); ++b)
  {
    ++sizes[partition.find(b)];
  }

  uint64_t islands = 0;
  uint64_t largest = 0;
  uint64_t inLarge = 0;

  for (const auto size : sizes)
  {
    if (size == 0)
    {
      continue;
    }

    ++islands;
    largest = std::max(largest, size);

    if (size >= 64)
    {
      inLarge += size;
    }
  }

  m_counters.hitIslands += islands;
  m_counters.largestHitIsland += largest;
  m_counters.largestHitIslandMax = std::max(m_counters.largestHitIslandMax, largest);
  m_counters.bodiesInLargeIslands += inLarge;
}

void CollisionSystem::recordSleepBlockers()
{
  const ScopedMicros timer(m_counters.diagMicros);

  for (const auto* body : m_bodies)
  {
    if (body->isAsleep())
    {
      continue;
    }

    ++m_counters.awakeBodies;

    const bool unsupported = body->getNextFalling();
    const float speed = glm::length(body->getVelocity());
    const float spin = glm::length(body->getAngularVelocity());

    m_counters.blockedUnsupported += unsupported ? 1 : 0;
    m_counters.blockedFast += speed >= sleeping::linearSleepSpeed ? 1 : 0;
    m_counters.blockedSpinning += spin >= sleeping::angularSleepSpeed ? 1 : 0;
    m_counters.blockedForces += body->getPendingForces().empty() ? 0 : 1;
    m_counters.blockedIsland += body->getRestTicks() >= sleeping::ticksToSleep ? 1 : 0;

    if (!unsupported)
    {
      ++m_counters.supportedSpeedHistogram[bucketOf(speed, speedBuckets)];
      ++m_counters.supportedSpinHistogram[bucketOf(spin, spinBuckets)];
    }
  }
}

void CollisionSystem::respondInParallel(const std::vector<size_t>& order,
                                        const std::vector<std::vector<std::shared_ptr<Object>>>& perEdgeCollisions,
                                        const HitBodies& hitBodies, const float dt, ResponseCounters& counters)
{
  const auto setupStart = std::chrono::steady_clock::now();
  constexpr auto noBody = std::numeric_limits<size_t>::max();

  // A response changes only the body it resolves and, when the other side is dynamic, that body's state, and
  // reads the Transforms up both ancestor chains. So bodies joined by any hit (a trigger's contact is measured
  // too, which reads the other collider), or by an ancestor that has its own body, share one component.
  BodyPartition partition(order.size());
  std::vector<size_t> bodyOfEdge(m_collisionEdges.size(), noBody);

  for (const auto i : order)
  {
    const auto& info = m_edgeInfos[i];
    if (info.asleep || !info.body)
    {
      continue;
    }

    const auto self = partition.indexOf(info.body);
    bodyOfEdge[i] = self;

    for (size_t k = 0; k < perEdgeCollisions[i].size(); ++k)
    {
      if (RigidBody* const otherBody = hitBodies.flat[hitBodies.offsets[i] + k].body)
      {
        const auto otherIndex = partition.indexOf(otherBody);
        partition.join(self, otherIndex);
      }
    }
  }

  // The loop can add bodies as it goes, and each new one is visited in turn.
  for (size_t b = 0; b < partition.size(); ++b)
  {
    const Object* owner = partition.bodyAt(b)->getOwner();
    if (!owner)
    {
      continue;
    }

    const auto parent = owner->getParent();
    if (!parent)
    {
      continue;
    }

    if (const auto ancestorBody = parent->getComponent<RigidBody>(ComponentType::rigidBody))
    {
      const auto ancestorIndex = partition.indexOf(ancestorBody.get());
      partition.join(b, ancestorIndex);
    }
  }

  std::vector<size_t> counts(partition.size(), 0);
  for (const auto i : order)
  {
    if (bodyOfEdge[i] != noBody)
    {
      ++counts[partition.find(bodyOfEdge[i])];
    }
  }

  std::vector<size_t> roots;
  for (size_t r = 0; r < counts.size(); ++r)
  {
    if (counts[r] > 0)
    {
      roots.push_back(r);
    }
  }

  std::ranges::sort(roots, [&counts](const size_t a, const size_t b)
  {
    return counts[a] > counts[b];
  });

  std::vector<size_t> slot(partition.size(), noBody);
  std::vector<size_t> begin(roots.size() + 1, 0);
  for (size_t c = 0; c < roots.size(); ++c)
  {
    slot[roots[c]] = c;
    begin[c + 1] = begin[c] + counts[roots[c]];
  }

  // Each component's edges keep the relative order the global response order gave them.
  std::vector<size_t> cursor(begin.begin(), begin.end() - 1);
  std::vector<size_t> edges(begin.back());
  for (const auto i : order)
  {
    if (bodyOfEdge[i] != noBody)
    {
      edges[cursor[slot[partition.find(bodyOfEdge[i])]]++] = i;
    }
  }

  m_counters.responseSetupMicros += microsSince(setupStart);
  m_counters.responseComponents += roots.size();
  if (!roots.empty())
  {
    m_counters.responseLargestComponent += counts[roots.front()];
  }

  const int componentCount = static_cast<int>(roots.size());
  const int threads = m_threadCount;
  std::vector<ResponseCounters> perComponent(roots.size());

#pragma omp parallel for num_threads(threads) schedule(dynamic, 1)
  for (int c = 0; c < componentCount; ++c)
  {
    auto& local = perComponent[static_cast<size_t>(c)];

    for (size_t e = begin[static_cast<size_t>(c)]; e < begin[static_cast<size_t>(c) + 1]; ++e)
    {
      const auto i = edges[e];
      const auto rigidBody = m_collisionEdges[i].object->getComponent<RigidBody>(ComponentType::rigidBody);
      if (!rigidBody)
      {
        continue;
      }

      handleCollisions(i, rigidBody, perEdgeCollisions[i], dt, local);
    }
  }

  for (const auto& local : perComponent)
  {
    counters.add(local);
  }
}

void CollisionSystem::setThreadCount(const int threads)
{
  m_threadCount = std::max(threads, 1);
  m_broadPhase.setThreadCount(m_threadCount);
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

      CachedContact entry{ true, std::move(result.contact), self.geometryKey, other.geometryKey };
      if (m_contactRefreshEnabled)
      {
        entry.hasPoses = true;
        entry.selfPose = poseOf(*self.collider);
        entry.otherPose = poseOf(*other.collider);
      }

      cachedContacts.push_back(std::move(entry));
    }
  }

  return stats;
}

void CollisionSystem::handleCollisions(const size_t edgeIndex, const std::shared_ptr<RigidBody>& rigidBody,
                                       const std::vector<std::shared_ptr<Object>>& collidedObjects, const float dt,
                                       ResponseCounters& counters) const
{
  const auto& collider = m_collisionEdges[edgeIndex].collider;

  if (collidedObjects.size() == 1)
  {
    // Triggers still recorded the pair (events fire), but get no MTV correction / impulse.
    if (isTriggerPair(collider, collidedObjects[0]))
    {
      return;
    }

    if (const auto contact = contactFor(edgeIndex, 0, collidedObjects[0], counters))
    {
      const ScopedMicros timer(counters.physicsMicros);
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
    auto contact = contactFor(edgeIndex, k, collidedObject, counters);
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
    const auto contact = bodyMoved ? contactFor(edgeIndex, scoredContact.index, scoredContact.object, counters)
                                   : scoredContact.contact;
    if (!contact)
    {
      continue;
    }

    {
      const ScopedMicros timer(counters.physicsMicros);
      PhysicsSystem::handleCollision(*rigidBody, scoredContact.object, contact->minimumTranslationVector,
                                     contact->contactPoints(), dt);
    }

    bodyMoved = true;
  }
}

std::optional<collisions::Contact> CollisionSystem::contactFor(const size_t edgeIndex, const size_t k,
                                                               const std::shared_ptr<Object>& other,
                                                               ResponseCounters& counters) const
{
  const auto& edge = m_collisionEdges[edgeIndex];
  const auto& cached = m_cachedContacts[edgeIndex];

  if (k < cached.size())
  {
    const auto& entry = cached[k];

    if (entry.computed && entry.selfKey == geometryKeyOf(*edge.object) &&
        entry.otherKey == geometryKeyOf(*other))
    {
      ++counters.contactsReused;
      return entry.contact;
    }

    if (m_contactRefreshEnabled && entry.computed && entry.contact && entry.hasPoses)
    {
      const ScopedMicros refreshTimer(counters.refreshMicros);
      const auto otherCollider = other->getComponent<Collider>(ComponentType::collider);

      if (otherCollider)
      {
        if (edge.collider->getColliderType() == ColliderType::sphereCollider &&
            otherCollider->getColliderType() == ColliderType::sphereCollider)
        {
          ++counters.refreshFellBackSphere;
        }
        else
        {
          auto refreshed = refreshContact(*entry.contact, entry.selfPose, entry.otherPose,
                                          poseOf(*edge.collider), poseOf(*otherCollider));

          switch (refreshed.outcome)
          {
            case RefreshOutcome::refreshed:
              ++counters.contactsRefreshed;
              return refreshed.contact;
            case RefreshOutcome::separated:
              ++counters.contactsRefreshed;
              return std::nullopt;
            case RefreshOutcome::rotationChanged:
              ++counters.refreshFellBackRotation;
              break;
            case RefreshOutcome::driftTooLarge:
              ++counters.refreshFellBackDrift;
              break;
            case RefreshOutcome::normalMotionTooLarge:
              ++counters.refreshFellBackNormalMotion;
              break;
          }
        }
      }
    }

    ++counters.contactsRecomputed;
  }

  const ScopedMicros recomputeTimer(counters.recomputeMicros);
  return contactWith(edge.collider, other);
}

RefreshResult CollisionSystem::refreshContact(const collisions::Contact& contact, const ColliderPose& selfThen,
                                              const ColliderPose& otherThen, const ColliderPose& selfNow,
                                              const ColliderPose& otherNow)
{
  if (selfThen.rotation != selfNow.rotation || selfThen.scale != selfNow.scale ||
      otherThen.rotation != otherNow.rotation || otherThen.scale != otherNow.scale)
  {
    return { RefreshOutcome::rotationChanged, std::nullopt };
  }

  const glm::vec3 dSelf = selfNow.position - selfThen.position;
  const glm::vec3 dOther = otherNow.position - otherThen.position;
  const glm::vec3 delta = dSelf - dOther;
  const glm::vec3 normal = contact.normal();
  const float along = glm::dot(delta, normal);

  if (glm::length(delta - normal * along) > refreshMaxTangentialDrift)
  {
    return { RefreshOutcome::driftTooLarge, std::nullopt };
  }

  if (std::abs(along) > refreshMaxNormalMotion)
  {
    return { RefreshOutcome::normalMotionTooLarge, std::nullopt };
  }

  const float depth = contact.depth() - along;
  if (!(depth > 0.0f))
  {
    return { RefreshOutcome::separated, std::nullopt };
  }

  const glm::vec3 shift = (dSelf + dOther) * 0.5f;

  collisions::Contact moved = contact;
  moved.minimumTranslationVector = normal * depth;
  moved.point += shift;

  for (size_t i = 0; i < moved.pointCount; ++i)
  {
    moved.points[i] += shift;
  }

  return { RefreshOutcome::refreshed, std::move(moved) };
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

  if (m_diagnosticsEnabled)
  {
    recordSleepBlockers();
  }
}

void CollisionSystem::pruneSleepRecords()
{
  std::erase_if(m_sleepRecords, [](const auto& entry)
  {
    return entry.second.collider.expired();
  });
}
