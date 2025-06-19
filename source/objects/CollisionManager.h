#ifndef COLLISIONMANAGER_H
#define COLLISIONMANAGER_H

#include <array>
#include <memory>
#include <vector>

#include "glm/vec3.hpp"

class Object;
class Collider;
class RigidBody;

struct LeftEdge {
  std::shared_ptr<Object> object;
  std::shared_ptr<Collider> collider;
  float position;
};

struct LineToRender {
  glm::vec3 start;
  glm::vec3 end;
};

class CollisionManager {
public:
  CollisionManager() = default;

  ~CollisionManager() = default;

  void addObject(const std::shared_ptr<Object>& object);

  void removeObject(const std::shared_ptr<Object>& object);

  void update();

  void variableUpdate();

private:
  std::vector<LeftEdge> collisionEdges;

  std::array<std::vector<LineToRender>, 6> threadLines = {};

  void checkCollisions();

  void findCollisions(const LeftEdge& edge, std::vector<std::shared_ptr<Object>>& collidedObjects, std::vector<LineToRender>& threadLine) const;

  static void handleCollisions(const std::shared_ptr<RigidBody>& rigidBody, const std::shared_ptr<Collider>& collider, const std::vector<std::shared_ptr<Object>>& collidedObjects);

};



#endif //COLLISIONMANAGER_H
