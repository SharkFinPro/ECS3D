#include "ECS3D.h"

#include "objects/ObjectManager.h"

ECS3D::ECS3D()
  : previousTime(std::chrono::steady_clock::now())
{
  initRenderer();
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

  if (objectManager != nullptr)
  {
    objectManager->update(std::min(dt, 0.01f));
  }

  renderer->render();
}

std::shared_ptr<VulkanEngine> ECS3D::getRenderer() const
{
  return renderer;
}

void ECS3D::setObjectManager(const std::shared_ptr<ObjectManager> &objectManager)
{
  this->objectManager = objectManager;
}

bool ECS3D::keyIsPressed(const int key) const
{
  return renderer->keyIsPressed(key);
}

void ECS3D::initRenderer()
{
  constexpr VulkanEngineOptions vulkanEngineOptions = {
    .WINDOW_WIDTH = 1280,
    .WINDOW_HEIGHT = 720,
    .WINDOW_TITLE = "ECS3D",
    .CAMERA_POSITION = { 0, 0, -50 },
    .FULLSCREEN = false
  };

  renderer = std::make_shared<VulkanEngine>(vulkanEngineOptions);

  // TODO: Add system to integrate lights into ECS
  renderer->createLight({0, 1.0f, 0}, {1.0f, 1.0f, 1.0f}, 0.25f, 0.0f, 0.0f);

  renderer->createLight({-10, -0.375f, 3}, {0.0f, 1.0f, 1.0f}, 0.0f, 0.75f, 0.75f);

  renderer->createLight({10, -0.375f, 3}, {1.0f, 0.0f, 0.0f}, 0.0f, 0.75f, 0.75f);
}
