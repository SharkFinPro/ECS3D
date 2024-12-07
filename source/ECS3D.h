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

  void setObjectManager(const std::shared_ptr<ObjectManager> &objectManager);

  [[nodiscard]] bool keyIsPressed(int key) const;

private:
  std::shared_ptr<VulkanEngine> renderer;

  std::chrono::time_point<std::chrono::steady_clock> previousTime;

  std::shared_ptr<ObjectManager> objectManager;

  void initRenderer();
};



#endif //ECS3D_H
