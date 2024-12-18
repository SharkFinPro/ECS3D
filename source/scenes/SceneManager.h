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

  void loadScene(int scene);

  [[nodiscard]] ECS3D* getECS() const;

  std::shared_ptr<Texture> getTexture(const std::string& path);
  std::shared_ptr<Model> getModel(const std::string& path);

  void update(float dt) const;

private:
  ECS3D* ecs;

  std::vector<std::shared_ptr<Scene>> scenes;

  int currentScene;

  std::unordered_map<std::string, std::shared_ptr<Texture>> textures;
  std::unordered_map<std::string, std::shared_ptr<Texture>> specularMaps;
  std::unordered_map<std::string, std::shared_ptr<Model>> models;
};



#endif //SCENEMANAGER_H
