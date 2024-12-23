#ifndef SCENEMANAGER_H
#define SCENEMANAGER_H

#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <VulkanEngine/VulkanEngine.h>

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
  ECS3D* ecs;

  std::vector<std::shared_ptr<Scene>> scenes;

  std::shared_ptr<Scene> currentScene;

  void sceneSelector();
};



#endif //SCENEMANAGER_H
