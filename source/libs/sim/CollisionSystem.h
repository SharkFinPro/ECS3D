#ifndef COLLISIONSYSTEM_H
#define COLLISIONSYSTEM_H

#include <glm/vec3.hpp>
#include <memory>
#include <vector>

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

class CollisionSystem {
public:
  void fixedUpdate(const ObjectManager& objectManager);

private:
  // Was CollisionManager::collisionEdges. The old add/removeObject maintenance is replaced by a
  // per-tick rebuild from the ObjectManager.
  std::vector<CollisionEdge> m_collisionEdges;

  void checkCollisions();

  void findCollisions(const CollisionEdge& edge, std::vector<std::shared_ptr<Object>>& collidedObjects) const;

  static void handleCollisions(const std::shared_ptr<RigidBody>& rigidBody, const std::shared_ptr<Collider>& collider,
                               const std::vector<std::shared_ptr<Object>>& collidedObjects);

  // GJK/EPA narrow phase, lifted out of Collider.
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
