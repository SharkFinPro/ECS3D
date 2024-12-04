#ifndef ECS3D_H
#define ECS3D_H

#include <chrono>
#include <memory>
#include <VulkanEngine/VulkanEngine.h>

class ObjectManager;

class ECS3D {
public:
  ECS3D();

  [[nodiscard]] bool isActive() const;

  void update();

  [[nodiscard]] std::shared_ptr<VulkanEngine> getRenderer() const;

  [[nodiscard]] std::shared_ptr<ObjectManager> getObjectManager() const;

private:
  std::shared_ptr<VulkanEngine> renderer;
  std::shared_ptr<ObjectManager> objectManager;

  std::chrono::time_point<std::chrono::steady_clock> previousTime;

  void initRenderer();

  void initObjectManager();
};



#endif //ECS3D_H
