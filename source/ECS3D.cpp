#include "ECS3D.h"

#include "scenes/SceneManager.h"

ECS3D::ECS3D()
  : previousTime(std::chrono::steady_clock::now()), sceneManager(std::make_shared<SceneManager>(this))
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

  sceneManager->update(dt);

  renderer->render();
}

std::shared_ptr<VulkanEngine> ECS3D::getRenderer() const
{
  return renderer;
}

bool ECS3D::keyIsPressed(const int key) const
{
  return renderer->keyIsPressed(key);
}

std::shared_ptr<SceneManager> ECS3D::getSceneManager() const
{
  return sceneManager;
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

  ImGui::SetCurrentContext(VulkanEngine::getImGuiContext());
}
