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

  void addObject(const std::shared_ptr<Object>& object);

  void startScene();

  void pauseScene();

  void resetScene();

private:
  ECS3D* m_ecs = nullptr;

  std::vector<std::shared_ptr<Object>> m_objects;

  std::shared_ptr<CollisionManager> m_collisionManager;

  std::shared_ptr<ObjectGUIManager> m_objectGUIManager;

  SceneStatus m_sceneStatus = SceneStatus::stopped;

  const float m_fixedUpdateDt = 1.0f / 50.0f;
  float m_timeAccumulator = 0.0f;

  void variableUpdate(float dt) const;
  void fixedUpdate(float dt);

  void displaySceneStatusGui();
};



#endif //OBJECTMANAGER_H
