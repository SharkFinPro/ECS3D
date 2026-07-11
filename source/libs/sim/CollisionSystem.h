#ifndef COLLISIONSYSTEM_H
#define COLLISIONSYSTEM_H

#include <glm/vec3.hpp>
#include <compare>
#include <memory>
#include <vector>
#include <uuid.h>

class ObjectManager;
class Object;
class Collider;
class RigidBody;
class Simplex;

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

class CollisionSystem {
public:
  void fixedUpdate(const ObjectManager& objectManager);

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
  std::vector<CollisionEdge> m_collisionEdges;

  // Sorted set of colliding pairs from the previous tick, diffed against the current tick to produce
  // the enter/stay/exit lists.
  std::vector<CollisionPair> m_previousPairs;
  std::vector<CollisionPair> m_enters;
  std::vector<CollisionPair> m_stays;
  std::vector<CollisionPair> m_exits;

  void checkCollisions();

  // Build this tick's sorted pair set from the per-edge collision results and diff it against the
  // previous tick to refresh m_enters/m_stays/m_exits.
  void recordCollisionEvents(const std::vector<std::vector<std::shared_ptr<Object>>>& perEdgeCollisions);

  void findCollisions(const CollisionEdge& edge, std::vector<std::shared_ptr<Object>>& collidedObjects) const;

  static void handleCollisions(const std::shared_ptr<RigidBody>& rigidBody, const std::shared_ptr<Collider>& collider,
                               const std::vector<std::shared_ptr<Object>>& collidedObjects);

  // A contact is a trigger (events fire, but no physical response) if either collider is flagged as one.
  static bool isTriggerPair(const std::shared_ptr<Collider>& collider, const std::shared_ptr<Object>& other);

  // Broad-phase layer filter: true only if each collider's mask includes the other's layer.
  static bool layersCollide(const std::shared_ptr<Collider>& a, const std::shared_ptr<Collider>& b);

  // GJK/EPA narrow phase.
  static bool collidesWith(const std::shared_ptr<Collider>& collider, const std::shared_ptr<Object>& other,
                           glm::vec3* mtv, glm::vec3* collisionPoint);

  static bool handleSphereToSphereCollision(const std::shared_ptr<Collider>& collider,
                                            const std::shared_ptr<Collider>& otherCollider,
                                            glm::vec3* mtv, glm::vec3* collisionPoint);

  static bool expandSimplex(Simplex& simplex, glm::vec3& direction);
  static void lineCase(const Simplex& simplex, glm::vec3& direction);
  static void triangleCase(Simplex& simplex, glm::vec3& direction);
  static bool tetrahedronCase(Simplex& simplex, glm::vec3& direction);
};



#endif //COLLISIONSYSTEM_H
