#ifndef ECS3D_H
#define ECS3D_H

#include <chrono>
#include <memory>
#include <VulkanEngine/VulkanEngine.h>

class SceneManager;

class ECS3D {
public:
  ECS3D();

  [[nodiscard]] bool isActive() const;

  void update();

  [[nodiscard]] std::shared_ptr<VulkanEngine> getRenderer() const;

  [[nodiscard]] bool keyIsPressed(int key) const;

  [[nodiscard]] std::shared_ptr<SceneManager> getSceneManager() const;

private:
  std::shared_ptr<VulkanEngine> renderer;

  std::chrono::time_point<std::chrono::steady_clock> previousTime;

  std::shared_ptr<SceneManager> sceneManager;

  void initRenderer();
};



#endif //ECS3D_H
