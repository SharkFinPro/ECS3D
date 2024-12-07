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

  void loadScene(int scene);

  [[nodiscard]] ECS3D* getECS() const;

private:
  ECS3D* ecs;

  std::vector<std::shared_ptr<Scene>> scenes;

  int currentScene;
};



#endif //SCENEMANAGER_H
