#include "SceneManager.h"
#include "Scene.h"
#include "../ECS3D.h"

SceneManager::SceneManager(ECS3D* ecs)
  : ecs(ecs), currentScene(0)
{}

std::shared_ptr<Scene> SceneManager::createScene()
{
  auto scene = std::make_shared<Scene>(this);

  scenes.push_back(scene);

  return scene;
}

void SceneManager::loadScene(const int scene)
{
  currentScene = scene;

  scenes[currentScene]->load();
}

ECS3D* SceneManager::getECS() const
{
  return ecs;
}

std::shared_ptr<Texture> SceneManager::getTexture(const std::string& path)
{
  if (!textures.contains(path))
  {
    textures.emplace(path, ecs->getRenderer()->loadTexture(path.c_str()));
  }

  return textures.at(path);
}

std::shared_ptr<Model> SceneManager::getModel(const std::string& path)
{
  if (!models.contains(path))
  {
    models.emplace(path, ecs->getRenderer()->loadModel(path.c_str()));
  }

  return models.at(path);
}

void SceneManager::update(const float dt)
{
  ImGui::Begin("Scene Selector");
  for (int i = 0; i < scenes.size(); i++)
  {
    if (ImGui::Selectable(("Scene " + std::to_string(i + 1)).c_str(), currentScene == i))
    {
      loadScene(i);
    }
  }
  ImGui::End();

  if (scenes.empty())
  {
    return;
  }

  scenes[currentScene]->update(dt);
}
