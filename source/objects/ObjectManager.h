#ifndef OBJECTMANAGER_H
#define OBJECTMANAGER_H

#include <vector>
#include <memory>

class Object;
class ECS3D;
class Collider;
class RigidBody;

struct LeftEdge {
  std::shared_ptr<Object> object;
  std::shared_ptr<Collider> collider;
  float position;
};

class ObjectManager {
public:
  ObjectManager();

  void update(float dt);

  void setECS(ECS3D* ecs);
  [[nodiscard]] ECS3D* getECS() const;

  void addObject(std::shared_ptr<Object> object);

  void resetObjects() const;

private:
  ECS3D* ecs;

  std::vector<std::shared_ptr<Object>> objects;

  std::vector<LeftEdge> collisionEdges;

  const float fixedUpdateDt;
  float timeAccumulator;

  std::shared_ptr<Object> selectedObject;

  void variableUpdate(float dt) const;
  void fixedUpdate(float dt);

  void checkCollisions();

  void findCollisions(const LeftEdge& edge, std::vector<std::shared_ptr<Object>>& collidedObjects) const;

  static void handleCollisions(const std::shared_ptr<RigidBody>& rigidBody, const std::shared_ptr<Collider>& collider, const std::vector<std::shared_ptr<Object>>& collidedObjects);

  void displayGui();
};



#endif //OBJECTMANAGER_H
