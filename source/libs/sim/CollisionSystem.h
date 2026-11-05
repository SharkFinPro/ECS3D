#ifndef COLLISIONSYSTEM_H
#define COLLISIONSYSTEM_H

#include "collisions/NarrowPhase.h"
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
  std::shared_ptr<RigidBody> body;
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

private:
  // A pair the forward sweep found: the higher edge's index, which dynamic side(s) saw the other, and the GJK
  // result each side found it with.
  struct SweepHit {
    size_t other;
    bool lowerSees;
    bool higherSees;
    std::optional<collisions::OverlapWitness> lowerWitness;
    std::optional<collisions::OverlapWitness> higherWitness;
  };

  // One edge's contact with another edge, and the GJK result that found it from this edge's side, which the
  // response hands back to the narrow phase.
  struct Touch {
    size_t other;
    std::optional<collisions::OverlapWitness> witness;
  };

  std::vector<CollisionEdge> m_collisionEdges;

  // Per-edge scratch, kept across ticks so its capacity carries over. Only the first m_collisionEdges.size()
  // entries belong to the current tick.
  std::vector<char> m_isDynamic;
  std::vector<std::vector<SweepHit>> m_forwardHits;
  std::vector<std::vector<Touch>> m_touches;

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

  // Indices of the edges with something to resolve, in the order their responses should run. Reads the
  // bounding boxes the sweep just warmed, so it has to run before any response moves a transform.
  [[nodiscard]] std::vector<size_t> responseOrder() const;

  // Forward sweep from one edge: appends a hit for every higher-indexed edge it meets, ascending. A pair is
  // visited once, from its lower-indexed edge, and only if at least one side is dynamic.
  void findCollisions(size_t index, const std::vector<char>& isDynamic, std::vector<SweepHit>& hits) const;

  // Resolves every touch the edge's body has, deepest first.
  void handleCollisions(size_t index, float dt) const;

  // A contact is a trigger (events fire, but no physical response) if either collider is flagged as one.
  static bool isTriggerPair(const Collider& collider, const Collider& other);

  // Broad-phase layer filter: true only if each collider's mask includes the other's layer.
  static bool layersCollide(const std::shared_ptr<Collider>& a, const std::shared_ptr<Collider>& b);
};



#endif //COLLISIONSYSTEM_H
