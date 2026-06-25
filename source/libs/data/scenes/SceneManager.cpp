#include "SceneManager.h"
#include "SceneAsset.h"
#include <stdexcept>

void SceneManager::addScene(const std::shared_ptr<SceneAsset>& scene)
{
  m_scenes.emplace(scene->getUUID(), scene);
}

void SceneManager::clear()
{
  m_scenes.clear();
  m_currentScene.reset();
  m_sceneStatus = SceneStatus::stopped;
}

std::shared_ptr<SceneAsset> SceneManager::getScene(const uuids::uuid& uuid) const
{
  const auto it = m_scenes.find(uuid);

  return it != m_scenes.end() ? it->second : nullptr;
}

const std::unordered_map<uuids::uuid, std::shared_ptr<SceneAsset>>& SceneManager::getScenes() const
{
  return m_scenes;
}

void SceneManager::loadScene(const std::shared_ptr<SceneAsset>& scene)
{
  if (!scene)
  {
    throw std::runtime_error("Attempted to load a scene that does not exist!");
  }

  resetScene();

  m_currentScene = scene;
}

std::shared_ptr<SceneAsset> SceneManager::getCurrentScene() const
{
  return m_currentScene;
}

void SceneManager::startScene()
{
  if (!m_currentScene)
  {
    return;
  }

  if (m_sceneStatus == SceneStatus::stopped)
  {
    m_currentScene->start();
  }

  m_sceneStatus = SceneStatus::running;
}

void SceneManager::pauseScene()
{
  if (!m_currentScene)
  {
    return;
  }

  m_sceneStatus = SceneStatus::paused;
}

void SceneManager::resetScene()
{
  if (!m_currentScene)
  {
    return;
  }

  if (m_sceneStatus == SceneStatus::running || m_sceneStatus == SceneStatus::paused)
  {
    m_currentScene->stop();
  }

  m_sceneStatus = SceneStatus::stopped;
}

SceneStatus SceneManager::getSceneStatus() const
{
  return m_sceneStatus;
}
