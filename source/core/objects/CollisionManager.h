#ifndef COLLISIONMANAGER_H
#define COLLISIONMANAGER_H

// Enable to draw collision detection test lines
// #define COLLISION_DEBUG

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

#ifdef COLLISION_DEBUG
#include <array>
#include "glm/vec3.hpp"

struct LineToRender {
  glm::vec3 start;
  glm::vec3 end;
};
#endif

class CollisionManager {
public:
  CollisionManager() = default;

  ~CollisionManager() = default;

  void addObject(const std::shared_ptr<Object>& object);

  void removeObject(const std::shared_ptr<Object>& object);

  void update();

#ifdef COLLISION_DEBUG
  void variableUpdate();
#endif

private:
  std::vector<LeftEdge> collisionEdges;

#ifdef COLLISION_DEBUG
  std::array<std::vector<LineToRender>, 6> threadLines = {};
#endif

  void checkCollisions();
#ifdef COLLISION_DEBUG
  void findCollisions(const LeftEdge& edge,
                      std::vector<std::shared_ptr<Object>>& collidedObjects,
                      std::vector<LineToRender>& threadLine) const;
#else
  void findCollisions(const LeftEdge& edge,
                      std::vector<std::shared_ptr<Object>>& collidedObjects) const;
#endif

  static void handleCollisions(const std::shared_ptr<RigidBody>& rigidBody,
                               const std::shared_ptr<Collider>& collider,
                               const std::vector<std::shared_ptr<Object>>& collidedObjects);
};



#endif //COLLISIONMANAGER_H
