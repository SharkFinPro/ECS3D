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

std::string SceneManager::uniqueSceneName(const uuids::uuid& uuid, const std::string& desiredName) const
{
  const std::string base = desiredName.empty() ? "Scene" : desiredName;

  // Strip a trailing " (N)" suffix so colliding with an already-suffixed name grows the number instead
  // of stacking another one (e.g. "Level (2)" collides into "Level (3)", never "Level (2) (2)").
  std::string stem = base;
  if (const auto open = base.rfind(" ("); open != std::string::npos && base.back() == ')')
  {
    const std::string inside = base.substr(open + 2, base.size() - open - 3);
    if (!inside.empty() && inside.find_first_not_of("0123456789") == std::string::npos)
    {
      stem = base.substr(0, open);
    }
  }

  const auto ownedByOther = [&](const std::string& candidate) {
    for (const auto& [otherUUID, otherScene] : m_scenes)
    {
      if (otherUUID != uuid && otherScene->getName() == candidate)
      {
        return true;
      }
    }
    return false;
  };

  if (!ownedByOther(base))
  {
    return base;
  }

  for (size_t n = 2;; ++n)
  {
    std::string candidate = stem + " (" + std::to_string(n) + ")";
    if (!ownedByOther(candidate))
    {
      return candidate;
    }
  }
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
