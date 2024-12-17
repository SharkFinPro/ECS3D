#include "SceneManager.h"
#include "Scene.h"
#include "../ECS3D.h"

SceneManager::SceneManager(ECS3D* ecs)
  : ecs(ecs), currentScene(-1)
{}

std::shared_ptr<Scene> SceneManager::createScene()
{
  auto scene = std::make_shared<Scene>(this);

  scenes.push_back(scene);

  return scene;
}

void SceneManager::loadScene(const int scene)
{
  if (currentScene != -1)
  {
    scenes[currentScene]->unload();
  }

  currentScene = scene - 1;

  scenes[currentScene]->load();
}

ECS3D* SceneManager::getECS() const
{
  return ecs;
}

std::shared_ptr<Texture> SceneManager::getTexture(const char* path)
{
  if (!textures.contains(path))
  {
    textures.emplace(path, ecs->getRenderer()->loadTexture(path));
  }

  return textures.at(path);
}

std::shared_ptr<Model> SceneManager::getModel(const char* path)
{
  if (!models.contains(path))
  {
    models.emplace(path, ecs->getRenderer()->loadModel(path));
  }

  return models.at(path);
}
