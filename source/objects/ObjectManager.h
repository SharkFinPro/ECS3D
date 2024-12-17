#ifndef OBJECTMANAGER_H
#define OBJECTMANAGER_H

#include <vector>
#include <memory>

class Object;
class ECS3D;
class Collider;
class RigidBody;

class ObjectManager {
public:
  ObjectManager();

  void update(float dt);

  void setECS(ECS3D* ecs);
  [[nodiscard]] ECS3D* getECS() const;

  void addObject(std::shared_ptr<Object> object);

  void enableRendering() const;

  void disableRendering() const;

  void resetObjects() const;

private:
  ECS3D* ecs;

  std::vector<std::shared_ptr<Object>> objects;

  const float fixedUpdateDt;
  float timeAccumulator;

  void variableUpdate(float dt) const;
  void fixedUpdate(float dt);

  void checkCollisions();

  void findCollisions(const std::shared_ptr<Object>& object, const std::shared_ptr<Collider>& collider, std::vector<std::shared_ptr<Object>>& collidedObjects);

  static void handleCollisions(const std::shared_ptr<RigidBody>& rigidBody, const std::shared_ptr<Collider>& collider, std::vector<std::shared_ptr<Object>>& collidedObjects);

};



#endif //OBJECTMANAGER_H
