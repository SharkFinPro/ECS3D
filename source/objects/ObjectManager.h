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

struct ObjectUINode {
  std::shared_ptr<Object> object;
  std::vector<std::shared_ptr<ObjectUINode>> children;
};

enum class SceneStatus {
  running,
  stopped,
  paused
};

class ObjectManager {
public:
  ObjectManager();

  void update(float dt);

  void setECS(ECS3D* ecs);
  [[nodiscard]] ECS3D* getECS() const;

  void addObject(const std::shared_ptr<Object>& object);

  void addObjectToCollisions(const std::shared_ptr<Object>& object);

  void removeObjectFromCollisions(const std::shared_ptr<Object>& object);

  void resetObjects() const;

  void startScene();

  void pauseScene();

  void resetScene();

private:
  ECS3D* ecs;

  std::vector<std::shared_ptr<Object>> objects;

  std::vector<LeftEdge> collisionEdges;

  std::vector<std::shared_ptr<ObjectUINode>> objectUINodes;

  const float fixedUpdateDt;
  float timeAccumulator;

  std::shared_ptr<Object> selectedObject;

  SceneStatus sceneStatus;

  void variableUpdate(float dt) const;
  void fixedUpdate(float dt);

  void checkCollisions();

  void findCollisions(const LeftEdge& edge, std::vector<std::shared_ptr<Object>>& collidedObjects) const;

  static void handleCollisions(const std::shared_ptr<RigidBody>& rigidBody, const std::shared_ptr<Collider>& collider, const std::vector<std::shared_ptr<Object>>& collidedObjects);

  void displayCreateObjectChildButton(const std::shared_ptr<ObjectUINode>& node);

  void displayObjectGui(const std::shared_ptr<ObjectUINode> &node);

  void displayGui();
};



#endif //OBJECTMANAGER_H
