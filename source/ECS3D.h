#ifndef ECS3D_H
#define ECS3D_H

#include <memory>
#include <VulkanEngine/VulkanEngine.h>

class ECS3D {
public:
  ECS3D();

  [[nodiscard]] bool isActive() const;

  void update();

private:
  std::shared_ptr<VulkanEngine> renderer;

  void initRenderer();
};



#endif //ECS3D_H
