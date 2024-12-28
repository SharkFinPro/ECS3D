#ifndef COLLISIONMANAGER_H
#define COLLISIONMANAGER_H

#include <memory>
#include <vector>

class Object;
class Collider;
class RigidBody;

struct LeftEdge {
  std::shared_ptr<Object> object;
  std::shared_ptr<Collider> collider;
  float position;
};

class CollisionManager {
public:
  CollisionManager() = default;

  ~CollisionManager() = default;

  void addObject(const std::shared_ptr<Object>& object);

  void removeObject(const std::shared_ptr<Object>& object);

  void update();

private:
  std::vector<LeftEdge> collisionEdges;

  void checkCollisions();

  void findCollisions(const LeftEdge& edge, std::vector<std::shared_ptr<Object>>& collidedObjects) const;

  static void handleCollisions(const std::shared_ptr<RigidBody>& rigidBody, const std::shared_ptr<Collider>& collider, const std::vector<std::shared_ptr<Object>>& collidedObjects);

};



#endif //COLLISIONMANAGER_H
