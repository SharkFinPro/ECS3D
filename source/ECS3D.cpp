#include "ECS3D.h"

#include "objects/ObjectManager.h"

ECS3D::ECS3D()
  : previousTime(std::chrono::steady_clock::now())
{
  initRenderer();
  initObjectManager();
}

bool ECS3D::isActive() const
{
  return renderer->isActive();
}

void ECS3D::update()
{
  const auto currentTime = std::chrono::steady_clock::now();
  const float dt = std::chrono::duration<float>(currentTime - previousTime).count();
  previousTime = currentTime;

  objectManager->update(dt);

  renderer->render();
}

std::shared_ptr<VulkanEngine> ECS3D::getRenderer() const
{
  return renderer;
}

std::shared_ptr<ObjectManager> ECS3D::getObjectManager() const
{
  return objectManager;
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

void ECS3D::initObjectManager()
{
  objectManager = std::make_shared<ObjectManager>();
  objectManager->setECS(this);
}
