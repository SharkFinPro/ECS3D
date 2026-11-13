#ifndef COLLISIONSYSTEM_H
#define COLLISIONSYSTEM_H

#include "ContactSolver.h"
#include "collisions/NarrowPhase.h"
#include <objects/components/collisions/Collider.h>
#include <glm/vec3.hpp>
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

struct CollisionEdge {
  std::shared_ptr<Object> object;
  std::shared_ptr<Collider> collider;
  float position;
  std::shared_ptr<RigidBody> body;

  // Copied from the collider once per tick, so the sweep's inner loop reads them from the edge array.
  BoundingBox bounds{};
  uint32_t layer = 0;
  uint32_t mask = 0;

  // How far the body of the collider moves this tick, which bounds is stretched along so the sweep finds what it
  // is about to reach.
  glm::vec3 sweep{ 0 };
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
  // dt is the tick length, which the contact solver needs to combine per-tick velocity with spin.
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

private:
  // A pair the forward sweep found: the higher edge's index, which dynamic side(s) saw the other, and the GJK
  // result each side found it with.
  struct SweepHit {
    size_t other;
    bool lowerSees;
    bool higherSees;
    std::optional<collisions::OverlapWitness> lowerWitness;
    std::optional<collisions::OverlapWitness> higherWitness;
    // Neither side overlaps the other yet, but they close on each other fast enough to meet this tick. Such a
    // pair is solved, so it meets without sinking in, and raises no collision event until it does overlap.
    bool approaching = false;
  };

  // A pair for the solver: the lower edge, the hit that names the higher one, and the pair's lowest bound.
  struct PairRef {
    size_t lower;
    const SweepHit* hit;
    float bottom;
  };

  // What the sweep's inner loop reads per candidate, packed so scanning neighbors streams through a few cache
  // lines instead of striding over whole CollisionEdges.
  struct SweepBox {
    BoundingBox bounds;
    uint32_t layer;
    uint32_t mask;
    bool dynamic;
  };

  std::vector<CollisionEdge> m_collisionEdges;

  // Per-edge scratch, kept across ticks so its capacity carries over. Only the first m_collisionEdges.size()
  // entries belong to the current tick. m_sweepBoxes is index-aligned with the sorted m_collisionEdges.
  std::vector<SweepBox> m_sweepBoxes;
  std::vector<std::vector<SweepHit>> m_forwardHits;
  // The edges each edge touches, ascending, which the collision events are built from.
  std::vector<std::vector<size_t>> m_touches;

  // Last tick's solved impulses by collider pair, which this tick's solve starts from.
  ContactSolver::Cache m_contactCache;

  // Sorted set of colliding pairs from the previous tick, diffed against the current tick to produce
  // the enter/stay/exit lists.
  std::vector<CollisionPair> m_previousPairs;
  std::vector<CollisionPair> m_enters;
  std::vector<CollisionPair> m_stays;
  std::vector<CollisionPair> m_exits;

  void checkCollisions(float dt);

  // Build this tick's sorted pair set from the per-edge touches and diff it against the previous tick to
  // refresh m_enters/m_stays/m_exits.
  void recordCollisionEvents();

  // Solves the contacts island by island: bodies linked by dynamic-vs-dynamic touches are solved together on
  // one thread, and separate islands in parallel.
  void resolveInIslands(float dt);

  // Measures the island's contacts and hands them to the solver.
  void solveIsland(const std::vector<PairRef>& island, std::vector<ContactSolver::CachedContact>& resolved,
                   float dt) const;

  // The displacements of first relative to second to look for an approach along: the two as they move now, then
  // each one moving alone.
  [[nodiscard]] static std::array<glm::vec3, 3> approachSweeps(const CollisionEdge& first, const CollisionEdge& second);

  // Moves a body that sank deep into static geometry this tick straight back out of it, as a fall fast enough to
  // pass most of the way through a thin floor would otherwise be pushed on out of the far side.
  static void liftOutOfStatic(std::vector<ContactPair>& pairs);

  // Forward sweep from one edge: appends a hit for every higher-indexed edge it meets, ascending. A pair is
  // visited once, from its lower-indexed edge, and only if at least one side is dynamic.
  void findCollisions(size_t index, std::vector<SweepHit>& hits) const;

  // A contact is a trigger (events fire, but no physical response) if either collider is flagged as one.
  static bool isTriggerPair(const Collider& collider, const Collider& other);

  // Broad-phase layer filter: true only if each collider's mask includes the other's layer.
  static bool layersCollide(const SweepBox& a, const SweepBox& b);
};



#endif //COLLISIONSYSTEM_H
