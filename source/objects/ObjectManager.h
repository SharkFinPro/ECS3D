#ifndef OBJECTMANAGER_H
#define OBJECTMANAGER_H

#include <vector>
#include <memory>

class Object;
class ECS3D;
class CollisionManager;
class ObjectGUIManager;

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

  [[nodiscard]] std::shared_ptr<CollisionManager> getCollisionManager() const;

  void addObject(const std::shared_ptr<Object>& object, bool createUI = true);

  void startScene();

  void pauseScene();

  void resetScene();

private:
  ECS3D* ecs;

  std::vector<std::shared_ptr<Object>> objects;

  std::shared_ptr<CollisionManager> collisionManager;

  std::shared_ptr<ObjectGUIManager> objectGUIManager;

  SceneStatus sceneStatus;

  const float fixedUpdateDt;
  float timeAccumulator;

  void variableUpdate(float dt) const;
  void fixedUpdate(float dt);

  void displaySceneStatusGui();
};



#endif //OBJECTMANAGER_H
