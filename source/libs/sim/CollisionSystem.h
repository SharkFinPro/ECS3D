#ifndef COLLISIONSYSTEM_H
#define COLLISIONSYSTEM_H

#include "broadphase/BroadPhase.h"
#include "collisions/NarrowPhase.h"
#include <objects/components/collisions/Collider.h>
#include <cstdint>
#include <compare>
#include <memory>
#include <optional>
#include <vector>
#include <uuid.h>

class ObjectManager;
class Object;
class Collider;
class RigidBody;

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

private:
  std::vector<CollisionEdge> m_collisionEdges;

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
  };

  // A contact the parallel pass computed for one hit, valid while both geometry keys still match.
  struct CachedContact {
    bool computed = false;
    std::optional<collisions::Contact> contact;
    uint64_t selfKey = 0;
    uint64_t otherKey = 0;
  };

  struct CandidateStats {
    uint64_t narrowPhaseCalls = 0;
    uint64_t contactsComputed = 0;
  };

  std::vector<EdgeInfo> m_edgeInfos;

  // Per edge, index-for-index with that edge's hit list; empty on the sweep path or with the cache off.
  std::vector<std::vector<CachedContact>> m_cachedContacts;

  bool m_contactCacheEnabled = true;

  BroadPhaseMode m_broadPhaseMode = BroadPhaseMode::tree;
  BroadPhase m_broadPhase;

  // Per edge: the edges whose tight AABB overlaps its own, ascending - the order the sweep visits them in.
  std::vector<std::vector<int32_t>> m_candidates;

  struct Counters {
    uint64_t ticks = 0;
    uint64_t candidates = 0;
    uint64_t narrowPhaseCalls = 0;
    uint64_t contactsComputed = 0;
    uint64_t contactsReused = 0;
    uint64_t contactsRecomputed = 0;
    uint64_t reinserts = 0;
    uint64_t gatherMicros = 0;
    uint64_t warmSortMicros = 0;
    uint64_t broadPhaseMicros = 0;
    uint64_t narrowMicros = 0;
    uint64_t responseMicros = 0;
    uint64_t eventsMicros = 0;
    uint64_t checkMicros = 0;
  };
  Counters m_counters;

  // Sorted set of colliding pairs from the previous tick, diffed against the current tick to produce
  // the enter/stay/exit lists.
  std::vector<CollisionPair> m_previousPairs;
  std::vector<CollisionPair> m_enters;
  std::vector<CollisionPair> m_stays;
  std::vector<CollisionPair> m_exits;

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
                                                            std::vector<CachedContact>& cachedContacts) const;

  void sweepCollisions(std::vector<std::vector<std::shared_ptr<Object>>>& perEdgeCollisions,
                       std::vector<uint64_t>& narrowPhaseCalls);

  void buildEdgeInfos();

  void updateBroadPhase();

  void collideWithCandidates(std::vector<std::vector<std::shared_ptr<Object>>>& perEdgeCollisions,
                             std::vector<uint64_t>& narrowPhaseCalls, std::vector<uint64_t>& contactsComputed);

  void reportCounters();

  void handleCollisions(size_t edgeIndex, const std::shared_ptr<RigidBody>& rigidBody,
                        const std::vector<std::shared_ptr<Object>>& collidedObjects, float dt);

  // The contact of edge edgeIndex's collider with its k-th hit: the cached one while both colliders'
  // geometry is unchanged since it was computed, otherwise contactWith.
  [[nodiscard]] std::optional<collisions::Contact> contactFor(size_t edgeIndex, size_t k,
                                                              const std::shared_ptr<Object>& other);

  // A contact is a trigger (events fire, but no physical response) if either collider is flagged as one.
  static bool isTriggerPair(const std::shared_ptr<Collider>& collider, const std::shared_ptr<Object>& other);

  // Broad-phase layer filter: true only if each collider's mask includes the other's layer.
  static bool layersCollide(const std::shared_ptr<Collider>& a, const std::shared_ptr<Collider>& b);

  // The narrow phase itself lives in collisions/NarrowPhase.h and works on a pair of colliders. This
  // resolves the other object's collider first, which is all the response path has to hand - the sweep
  // already holds both colliders and calls the narrow phase directly.
  [[nodiscard]] static std::optional<collisions::Contact> contactWith(const std::shared_ptr<Collider>& collider,
                                                                      const std::shared_ptr<Object>& other);
};



#endif //COLLISIONSYSTEM_H
