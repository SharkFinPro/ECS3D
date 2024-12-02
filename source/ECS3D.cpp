#include "ECS3D.h"

ECS3D::ECS3D()
{
  initRenderer();
}

bool ECS3D::isActive() const
{
  return renderer->isActive();
}

void ECS3D::update()
{
  renderer->render();
}

void ECS3D::initRenderer()
{
  constexpr VulkanEngineOptions vulkanEngineOptions = {
    .WINDOW_WIDTH = 800,
    .WINDOW_HEIGHT = 600,
    .WINDOW_TITLE = "ECS3D"
  };

  renderer = std::make_shared<VulkanEngine>(vulkanEngineOptions);
}
