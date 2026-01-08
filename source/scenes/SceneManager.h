#ifndef SCENEMANAGER_H
#define SCENEMANAGER_H

#include <memory>
#include <vector>

class ECS3D;
class Scene;

class SceneManager {
public:
  explicit SceneManager(ECS3D* ecs);

  std::shared_ptr<Scene> createScene();

  void loadScene(const std::shared_ptr<Scene>& scene);

  [[nodiscard]] ECS3D* getECS() const;

  void update(float dt);

private:
  ECS3D* m_ecs;

  std::vector<std::shared_ptr<Scene>> m_scenes;

  std::shared_ptr<Scene> m_currentScene = nullptr;

  void sceneSelector();
};



#endif //SCENEMANAGER_H
