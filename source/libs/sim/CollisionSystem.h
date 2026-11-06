#ifndef COLLISIONSYSTEM_H
#define COLLISIONSYSTEM_H

#include "broadphase/BroadPhase.h"
#include "collisions/NarrowPhase.h"
#include <objects/components/collisions/Collider.h>
#include <array>
#include <bit>
#include <compare>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>
#include <uuid.h>

class ObjectManager;
class Object;
class Collider;
class RigidBody;

// One collider taking part in collision detection. Edges persist across ticks while the scene's colliders
// stay the same, each holding its broad-phase proxy.
struct CollisionEdge {
  std::shared_ptr<Object> object;
  std::shared_ptr<Collider> collider;
  std::shared_ptr<RigidBody> body;
  int32_t proxy = BroadPhase::nullProxy;

  // Copied from the collider once per tick, so the parallel pass reads them from the edge array.
  BoundingBox bounds{};
  uint32_t layer = 0;
  uint32_t mask = 0;
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

  // Lexicographic by (a, b) over their bytes, the order uuid's own < gives, compared eight bytes at a time:
  // the event diff sorts and merges these every tick. The full set of relational operators makes
  // CollisionPair model totally_ordered, as std::ranges::sort and set_difference require.
  std::strong_ordering operator<=>(const CollisionPair& other) const
  {
    const auto mine = words();
    const auto theirs = other.words();
    for (size_t i = 0; i < mine.size(); ++i)
    {
      if (mine[i] != theirs[i])
      {
        return mine[i] < theirs[i] ? std::strong_ordering::less : std::strong_ordering::greater;
      }
    }

    return std::strong_ordering::equal;
  }

  bool operator==(const CollisionPair& other) const = default;

private:
  // Big-endian, so comparing the words compares the bytes in order.
  [[nodiscard]] static uint64_t wordAt(const uuids::uuid& id, const size_t offset)
  {
    uint64_t word = 0;
    std::memcpy(&word, id.as_bytes().data() + offset, sizeof(word));
    if constexpr (std::endian::native == std::endian::little)
    {
#if defined(_MSC_VER) && !defined(__clang__)
      word = _byteswap_uint64(word);
#else
      word = __builtin_bswap64(word);
#endif
    }

    return word;
  }

  [[nodiscard]] std::array<uint64_t, 4> words() const
  {
    return { wordAt(a, 0), wordAt(a, 8), wordAt(b, 0), wordAt(b, 8) };
  }
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
  // previous run don't leak into the next run's first diff as spurious enter/exit events. Also drops the
  // broad phase, which the next tick rebuilds.
  void reset();

  // The persistent broad phase, for tests and the benchmark.
  [[nodiscard]] const BroadPhase& getBroadPhase() const { return m_broadPhase; }

private:
  // One broad-phase pair's narrow-phase result: which dynamic side(s) saw the other, and the GJK result each
  // side found it with.
  struct PairHit {
    size_t first = 0;
    size_t second = 0;
    bool firstSees = false;
    bool secondSees = false;
    std::optional<collisions::OverlapWitness> firstWitness;
    std::optional<collisions::OverlapWitness> secondWitness;
  };

  // One edge's contact with another edge, and the GJK result that found it from this edge's side, which the
  // response hands back to the narrow phase.
  struct Touch {
    size_t other;
    std::optional<collisions::OverlapWitness> witness;
  };

  // In ObjectManager order, the order every tie in contact resolution falls back to.
  std::vector<CollisionEdge> m_collisionEdges;
  BroadPhase m_broadPhase;

  // Scratch kept across ticks so its capacity carries over. m_touches holds one list per edge; only the first
  // m_collisionEdges.size() entries belong to the current tick.
  std::vector<PairHit> m_pairHits;
  std::vector<std::vector<Touch>> m_touches;
  // Each edge's min x for this tick, NaN sorted last: the key of every sweep-order comparison.
  std::vector<float> m_sweepKeys;

  // Sorted set of colliding pairs from the previous tick, diffed against the current tick to produce
  // the enter/stay/exit lists.
  std::vector<CollisionPair> m_previousPairs;
  std::vector<CollisionPair> m_enters;
  std::vector<CollisionPair> m_stays;
  std::vector<CollisionPair> m_exits;

  // Brings m_collisionEdges and the broad-phase proxies in line with the scene's colliders.
  void gatherEdges(const ObjectManager& objectManager);

  // Whether the scene still holds exactly the colliders, owners and bodies of m_collisionEdges, in order.
  [[nodiscard]] bool edgesMatch(const ObjectManager& objectManager) const;

  void rebuildEdges(const ObjectManager& objectManager);

  void checkCollisions(float dt);

  // Refreshes every edge's bounds, layer, mask and sweep key from its collider, warming the collider caches
  // the parallel narrow phase reads.
  void warmEdges();

  // Fills each touched edge's list from the pair hits, ordered by the other edge's sweep position.
  void collectTouches();

  // Build this tick's sorted pair set from the per-edge touches and diff it against the previous tick to
  // refresh m_enters/m_stays/m_exits.
  void recordCollisionEvents();

  // Indices of the edges with something to resolve, in the order their responses should run. Reads the
  // bounding boxes warmed at the start of the tick, so it has to run before any response moves a transform.
  [[nodiscard]] std::vector<size_t> responseOrder() const;

  // The order a sweep over min x would visit edges a and b in: by min x, ties by edge index.
  [[nodiscard]] bool sweepsBefore(size_t a, size_t b) const;

  // Runs the filters and the narrow phase on one broad-phase pair. Read-only on everything but its own hit.
  void testPair(uint64_t pair, PairHit& hit) const;

  // Resolves every touch the edge's body has, deepest first.
  void handleCollisions(size_t index, float dt) const;

  // A contact is a trigger (events fire, but no physical response) if either collider is flagged as one.
  static bool isTriggerPair(const Collider& collider, const Collider& other);

  // Broad-phase layer filter: true only if each collider's mask includes the other's layer.
  static bool layersCollide(const CollisionEdge& a, const CollisionEdge& b);
};



#endif //COLLISIONSYSTEM_H
