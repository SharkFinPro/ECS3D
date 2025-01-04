#include "SceneManager.h"
#include "Scene.h"
#include "../ECS3D.h"
#include <stdexcept>

SceneManager::SceneManager(ECS3D* ecs)
  : ecs(ecs), currentScene(nullptr)
{}

std::shared_ptr<Scene> SceneManager::createScene()
{
  auto scene = std::make_shared<Scene>(this);

  scenes.push_back(scene);

  if (!currentScene)
  {
    loadScene(scene);
  }

  return scene;
}

void SceneManager::loadScene(const std::shared_ptr<Scene>& scene)
{
  if (!scene)
  {
    throw std::runtime_error("Attempted to load a scene that does not exist!");
  }

  currentScene = scene;

  currentScene->load();
}

ECS3D* SceneManager::getECS() const
{
  return ecs;
}

void SceneManager::update(const float dt)
{
  sceneSelector();

  if (!currentScene)
  {
    return;
  }

  currentScene->update(dt);
}

void SceneManager::sceneSelector()
{
  ImGui::Begin("Scene Selector");
  for (int i = 0; i < scenes.size(); ++i)
  {
    if (ImGui::Selectable(("Scene " + std::to_string(i + 1)).c_str(), currentScene == scenes[i]))
    {
      loadScene(scenes[i]);
    }
  }
  ImGui::End();
}
