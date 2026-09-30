#ifndef COLLISIONSYSTEM_H
#define COLLISIONSYSTEM_H

#include "PhysicsSystem.h"
#include "broadphase/BroadPhase.h"
#include "collisions/NarrowPhase.h"
#include <objects/components/collisions/Collider.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <compare>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>
#include <glm/vec3.hpp>
#include <uuid.h>

class ObjectManager;
class Object;
class Collider;
class RigidBody;
class Transform;

struct CollisionEdge {
  std::shared_ptr<Object> object;
  std::shared_ptr<Collider> collider;
  float position;
};

// An unordered colliding pair, stored canonically (a < b) so the same contact recorded from either
// object's side dedupes to one entry. Plain uuids so the app can hand collision events to ScriptSystem
// without sim depending on scripting.
struct CollisionPair {
  uuids::uuid a;
  uuids::uuid b;

  static CollisionPair make(const uuids::uuid& x, const uuids::uuid& y)
  {
    return x < y ? CollisionPair{x, y} : CollisionPair{y, x};
  }

  // Lexicographic by (a, b). Defaulting the three-way comparison gives the full set of relational
  // operators (uuid has only < and ==, which the compiler synthesizes from), so CollisionPair models
  // totally_ordered - required by std::ranges::sort / set_difference.
  std::strong_ordering operator<=>(const CollisionPair& other) const = default;
};

enum class BroadPhaseMode {
  sweep,
  tree
};

// A collider's world placement at one moment, as the contact refresh compares it.
struct ColliderPose {
  glm::vec3 position{0.0f};
  glm::vec3 rotation{0.0f};
  glm::vec3 scale{1.0f};
};

enum class RefreshOutcome {
  refreshed,
  separated,
  rotationChanged,
  driftTooLarge,
  normalMotionTooLarge
};

struct RefreshResult {
  RefreshOutcome outcome = RefreshOutcome::rotationChanged;
  std::optional<collisions::Contact> contact;
};

class CollisionSystem {
public:
  // dt is the tick length, which contact responses need to combine per-tick velocity with spin.
  void fixedUpdate(const ObjectManager& objectManager, float dt);

  // Collision events for the most recent tick, diffed against the tick before it. enters = pairs new
  // this tick, stays = pairs present both ticks, exits = pairs gone this tick. Sorted; consumed by the
  // app to dispatch onCollisionEnter/Stay/Exit into scripts.
  [[nodiscard]] const std::vector<CollisionPair>& getCollisionEnters() const { return m_enters; }
  [[nodiscard]] const std::vector<CollisionPair>& getCollisionStays() const { return m_stays; }
  [[nodiscard]] const std::vector<CollisionPair>& getCollisionExits() const { return m_exits; }

  // Clear the recorded pair history + event lists. Call on a scene start/stop/switch so contacts from a
  // previous run don't leak into the next run's first diff as spurious enter/exit events.
  void reset();

  // Switching modes drops the tree's proxies and pair cache, so an A/B comparison starts clean.
  void setBroadPhaseMode(BroadPhaseMode mode);
  [[nodiscard]] BroadPhaseMode getBroadPhaseMode() const { return m_broadPhaseMode; }

  // On (the default), the tree path computes each contact in the parallel pass and the response pass
  // reuses it while both colliders' geometry is unchanged. Off, the response pass always recomputes.
  void setContactCacheEnabled(bool enabled) { m_contactCacheEnabled = enabled; }
  [[nodiscard]] bool isContactCacheEnabled() const { return m_contactCacheEnabled; }

  // Off by default. On, and in tree mode, a settled island of bodies stops costing anything until something
  // disturbs it. Sweep mode never sleeps anything.
  void setSleepingEnabled(bool enabled);
  [[nodiscard]] bool isSleepingEnabled() const { return m_sleepingEnabled; }

  // Off by default. On, the response pass moves a cached contact along with a translation of either body
  // instead of running GJK/EPA again, while the drift stays small (see refreshContact). Approximate.
  void setContactRefreshEnabled(bool enabled) { m_contactRefreshEnabled = enabled; }
  [[nodiscard]] bool isContactRefreshEnabled() const { return m_contactRefreshEnabled; }

  // Refreshed contacts since construction; unlike the periodic stats it is never reset.
  [[nodiscard]] uint64_t getContactsRefreshedTotal() const { return m_contactsRefreshedTotal; }

  // Threads for every tree-path parallel region (bounding box warm, edge data, broad phase queries, narrow
  // phase, parallel response). The sweep path keeps its own fixed count. Values under one are clamped to one.
  void setThreadCount(int threads);
  [[nodiscard]] int getThreadCount() const { return m_threadCount; }

  // Off by default. On, and in tree mode, the response pass resolves each connected component of dynamic
  // bodies on its own thread, in the order the serial pass would visit its edges. Bit-exact with the serial pass.
  void setParallelResponseEnabled(bool enabled) { m_parallelResponseEnabled = enabled; }
  [[nodiscard]] bool isParallelResponseEnabled() const { return m_parallelResponseEnabled; }

  // Off by default. On, the response pass times each contact's stages (the resp_* entries of the stats line) and
  // each tick runs the island and sleep-blocker analysis behind it (diag_us). Off, the hot path reads no clock
  // per contact; the per-stage timers stay on. Never changes what the simulation computes.
  void setDetailedTimingEnabled(bool enabled) { m_detailedTiming = enabled; }
  [[nodiscard]] bool isDetailedTimingEnabled() const { return m_detailedTiming; }

  // Carries a contact computed at the "then" poses to the "now" poses, assuming both colliders only
  // translated. Falls back (contact empty, outcome says why) when a rotation or scale changed or the
  // motion is large enough that the contact features may have changed; outcome separated with an empty
  // contact means the push cleared the overlap. Public so a test can compare it with findContact.
  [[nodiscard]] static RefreshResult refreshContact(const collisions::Contact& contact,
                                                    const ColliderPose& selfThen, const ColliderPose& otherThen,
                                                    const ColliderPose& selfNow, const ColliderPose& otherNow);

private:
  std::vector<CollisionEdge> m_collisionEdges;

  // What the response pass counts. One per thread (or per component) in the parallel pass, summed afterward.
  struct ResponseCounters {
    uint64_t contactsReused = 0;
    uint64_t contactsRecomputed = 0;
    uint64_t contactsRefreshed = 0;
    uint64_t refreshFellBackRotation = 0;
    uint64_t refreshFellBackDrift = 0;
    uint64_t refreshFellBackNormalMotion = 0;
    uint64_t refreshFellBackSphere = 0;
    uint64_t recomputeMicros = 0;
    uint64_t refreshMicros = 0;
    uint64_t physicsMicros = 0;
    uint64_t keysMicros = 0;
    uint64_t scoreMicros = 0;
    uint64_t lookupMicros = 0;

    void add(const ResponseCounters& other)
    {
      contactsReused += other.contactsReused;
      contactsRecomputed += other.contactsRecomputed;
      contactsRefreshed += other.contactsRefreshed;
      refreshFellBackRotation += other.refreshFellBackRotation;
      refreshFellBackDrift += other.refreshFellBackDrift;
      refreshFellBackNormalMotion += other.refreshFellBackNormalMotion;
      refreshFellBackSphere += other.refreshFellBackSphere;
      recomputeMicros += other.recomputeMicros;
      refreshMicros += other.refreshMicros;
      physicsMicros += other.physicsMicros;
      keysMicros += other.keysMicros;
      scoreMicros += other.scoreMicros;
      lookupMicros += other.lookupMicros;
    }
  };

  // One entry per hit of an edge, in hit order: the rigid body behind the other object (null for static
  // geometry) and whether its collider is a trigger. offsets has one more entry than there are edges.
  struct HitBody {
    RigidBody* body = nullptr;
    bool trigger = false;
  };

  struct HitBodies {
    std::vector<size_t> offsets;
    std::vector<HitBody> flat;
  };

  // Plain per-edge data, parallel to m_collisionEdges (tree path only), so the parallel pass rejects
  // candidates without touching a shared_ptr.
  struct EdgeInfo {
    Object* object = nullptr;
    Object* parent = nullptr;
    Collider* collider = nullptr;
    bool hasRigidBody = false;
    uint32_t layer = 0;
    uint32_t mask = 0;
    BoundingBox box;
    bool trigger = false;
    uint64_t geometryKey = 0;
    RigidBody* body = nullptr;
    bool asleep = false;
    uint32_t islandId = 0;
    size_t bodyIndex = 0;

    // The Transform of the body's owner, and the Collider of that owner, which for a child collider are not the
    // edge's own.
    Transform* bodyTransform = nullptr;
    Collider* bodyCollider = nullptr;

    // The Transforms geometryKeyOf sums: the object's own, then each ancestor up to the first without one. A
    // chain deeper than keyChainCapacity leaves keyChainOverflow set and is walked the long way.
    static constexpr size_t keyChainCapacity = 4;
    std::array<const Transform*, keyChainCapacity> keyChain{};
    uint8_t keyChainCount = 0;
    bool keyChainOverflow = false;
  };

  // What an island's edge reported when the island fell asleep, replayed while it sleeps.
  struct SleepHit {
    std::weak_ptr<Object> object;
    uint64_t key = 0;
    bool dynamic = false;
    bool trigger = false;
  };

  struct SleepRecord {
    std::weak_ptr<Collider> collider;
    uint32_t islandId = 0;
    uint64_t selfKey = 0;
    std::vector<SleepHit> hits;
  };

  // A contact the parallel pass computed for one hit, valid while both geometry keys still match.
  struct CachedContact {
    bool computed = false;
    std::optional<collisions::Contact> contact;
    uint64_t selfKey = 0;
    uint64_t otherKey = 0;
    bool hasPoses = false;
    ColliderPose selfPose;
    ColliderPose otherPose;
  };

  struct CandidateStats {
    uint64_t narrowPhaseCalls = 0;
    uint64_t contactsComputed = 0;
  };

  std::vector<EdgeInfo> m_edgeInfos;

  // Per edge, index-for-index with that edge's hit list: the hit's own edge. Empty on the sweep path.
  std::vector<std::vector<int32_t>> m_hitEdges;

  // Per edge, index-for-index with that edge's hit list; empty on the sweep path or with the cache off.
  std::vector<std::vector<CachedContact>> m_cachedContacts;

  bool m_contactCacheEnabled = true;
  bool m_contactRefreshEnabled = false;
  uint64_t m_contactsRefreshedTotal = 0;

  BroadPhaseMode m_broadPhaseMode = BroadPhaseMode::tree;
  BroadPhase m_broadPhase;

  // Per edge: the edges whose tight AABB overlaps its own, ascending - the order the sweep visits them in.
  std::vector<std::vector<int32_t>> m_candidates;

  struct Counters {
    uint64_t ticks = 0;
    uint64_t candidates = 0;
    uint64_t narrowPhaseCalls = 0;
    uint64_t contactsComputed = 0;
    ResponseCounters response;
    uint64_t reinserts = 0;
    uint64_t sleepSkippedEdges = 0;
    uint64_t fellAsleep = 0;
    uint64_t woke = 0;
    uint64_t bodiesAsleep = 0;
    uint64_t islandsAsleep = 0;
    uint64_t gatherMicros = 0;
    uint64_t warmSortMicros = 0;
    uint64_t broadPhaseMicros = 0;
    uint64_t narrowMicros = 0;
    uint64_t responseMicros = 0;
    uint64_t responseOrderMicros = 0;
    uint64_t eventsMicros = 0;
    uint64_t checkMicros = 0;
    uint64_t hitGraphMicros = 0;
    uint64_t responseSetupMicros = 0;
    uint64_t diagMicros = 0;
    uint64_t responseComponents = 0;
    uint64_t responseLargestComponent = 0;
    uint64_t hitIslands = 0;
    uint64_t largestHitIsland = 0;
    uint64_t largestHitIslandMax = 0;
    uint64_t bodiesInLargeIslands = 0;
    uint64_t awakeBodies = 0;
    uint64_t blockedUnsupported = 0;
    uint64_t blockedFast = 0;
    uint64_t blockedSpinning = 0;
    uint64_t blockedForces = 0;
    uint64_t blockedIsland = 0;
    std::array<uint64_t, 6> supportedSpeedHistogram{};
    std::array<uint64_t, 5> supportedSpinHistogram{};
  };
  Counters m_counters;

  int m_threadCount = 6;
  bool m_parallelResponseEnabled = false;
  bool m_detailedTiming = false;

  // Sorted set of colliding pairs from the previous tick, diffed against the current tick to produce
  // the enter/stay/exit lists.
  std::vector<CollisionPair> m_previousPairs;
  std::vector<CollisionPair> m_enters;
  std::vector<CollisionPair> m_stays;
  std::vector<CollisionPair> m_exits;

  bool m_sleepingEnabled = false;
  bool m_wakeAll = false;
  uint32_t m_nextIslandId = 1;
  uint64_t m_asleepBodiesLastTick = 0;

  // Keyed by an island member's collider; the weak_ptr inside guards against address reuse.
  std::unordered_map<const Collider*, SleepRecord> m_sleepRecords;

  // This tick's edges by object, and the distinct rigid bodies behind them (tree path, sleeping on).
  std::unordered_map<const Object*, int32_t> m_edgeOfObject;
  std::vector<RigidBody*> m_bodies;

  void checkCollisions(float dt);

  // Build this tick's sorted pair set from the per-edge collision results and diff it against the
  // previous tick to refresh m_enters/m_stays/m_exits.
  void recordCollisionEvents(const std::vector<std::vector<std::shared_ptr<Object>>>& perEdgeCollisions);

  // Indices of the edges with something to resolve, in the order their responses should run. Reads the
  // bounding boxes the sweep just warmed, so it has to run before any response moves a transform.
  [[nodiscard]] std::vector<size_t> responseOrder(
    const std::vector<std::vector<std::shared_ptr<Object>>>& perEdgeCollisions) const;

  // Returns how many times it called the narrow phase.
  [[nodiscard]] uint64_t findCollisions(const CollisionEdge& edge,
                                        std::vector<std::shared_ptr<Object>>& collidedObjects) const;

  // Same filters and order as findCollisions, over the broad phase's candidate list instead of a sweep,
  // reading the lean per-edge data. With the contact cache on, cachedContacts gets one entry per hit.
  [[nodiscard]] CandidateStats findCollisionsFromCandidates(size_t edgeIndex,
                                                            const std::vector<int32_t>& candidates,
                                                            std::vector<std::shared_ptr<Object>>& collidedObjects,
                                                            std::vector<int32_t>& hitEdges,
                                                            std::vector<CachedContact>& cachedContacts,
                                                            std::vector<uint32_t>& wakeIslands) const;

  // The still-live hits an asleep edge reported when it fell asleep. Asks for its island to wake when
  // anything they were measured against has changed.
  void collectSleepingHits(size_t edgeIndex, std::vector<std::shared_ptr<Object>>& collidedObjects,
                           std::vector<int32_t>& hitEdges, std::vector<uint32_t>& wakeIslands) const;

  void applyWakeRequests(const std::vector<std::vector<uint32_t>>& requests);

  void wakeEverything();

  void updateSleeping(const std::vector<std::vector<std::shared_ptr<Object>>>& perEdgeCollisions);

  void pruneSleepRecords();

  // Drops every record and island, and wakes every body on the next tick.
  void clearSleepState();

  void sweepCollisions(std::vector<std::vector<std::shared_ptr<Object>>>& perEdgeCollisions,
                       std::vector<uint64_t>& narrowPhaseCalls);

  void buildEdgeInfos();

  // What geometryKeyOf returns for the edge's object, from the Transforms buildEdgeInfos resolved.
  [[nodiscard]] static uint64_t keyOf(const EdgeInfo& info);

  // Tree mode. Filled in parallel; read by the island diagnostics and the parallel response.
  [[nodiscard]] HitBodies collectHitBodies(
    const std::vector<std::vector<std::shared_ptr<Object>>>& perEdgeCollisions) const;

  void recordHitIslands(const std::vector<std::vector<std::shared_ptr<Object>>>& perEdgeCollisions,
                        const HitBodies& hitBodies);

  void recordSleepBlockers();

  // Resolves the response pass in parallel, one connected component of bodies per task, and adds what the
  // tasks counted to counters. Every edge of the order lands in exactly one component.
  void respondInParallel(const std::vector<size_t>& order,
                         const std::vector<std::vector<std::shared_ptr<Object>>>& perEdgeCollisions,
                         const HitBodies& hitBodies, float dt, ResponseCounters& counters);

  void updateBroadPhase();

  void collideWithCandidates(std::vector<std::vector<std::shared_ptr<Object>>>& perEdgeCollisions,
                             std::vector<uint64_t>& narrowPhaseCalls, std::vector<uint64_t>& contactsComputed,
                             std::vector<std::vector<uint32_t>>& wakeRequests);

  void reportCounters();

  // The tree path resolves each hit's components from m_edgeInfos; the sweep path, with no hit edges, looks them up.
  void handleCollisions(size_t edgeIndex, RigidBody& rigidBody,
                        const std::vector<std::shared_ptr<Object>>& collidedObjects, float dt,
                        ResponseCounters& counters) const;

  // The contact of edge edgeIndex's collider with its k-th hit: the cached one while both colliders'
  // geometry is unchanged since it was computed, otherwise contactWith. otherInfo is the hit's edge, or null
  // when there is none.
  [[nodiscard]] std::optional<collisions::Contact> contactFor(size_t edgeIndex, size_t k,
                                                              const std::shared_ptr<Object>& other,
                                                              const EdgeInfo* otherInfo,
                                                              ResponseCounters& counters) const;

  // The components PhysicsSystem::handleCollision reads for the pair: read from the two edges when both are known,
  // looked up otherwise.
  [[nodiscard]] static PhysicsSystem::Parties partiesOf(const EdgeInfo* self, const EdgeInfo* other, RigidBody& body,
                                                        const std::shared_ptr<Object>& otherObject);

  // A contact is a trigger (events fire, but no physical response) if either collider is flagged as one.
  static bool isTriggerPair(const std::shared_ptr<Collider>& collider, const EdgeInfo* otherInfo,
                            const std::shared_ptr<Object>& other);

  // Broad-phase layer filter: true only if each collider's mask includes the other's layer.
  static bool layersCollide(const std::shared_ptr<Collider>& a, const std::shared_ptr<Collider>& b);

  // The narrow phase itself lives in collisions/NarrowPhase.h and works on a pair of colliders. This
  // resolves the other object's collider first, which is all the response path has to hand - the sweep
  // already holds both colliders and calls the narrow phase directly.
  [[nodiscard]] static std::optional<collisions::Contact> contactWith(const std::shared_ptr<Collider>& collider,
                                                                      const std::shared_ptr<Object>& other);
};



#endif //COLLISIONSYSTEM_H
