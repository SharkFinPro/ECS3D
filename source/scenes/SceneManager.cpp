#include "SceneManager.h"
#include "../assets/SceneAsset.h"
#include <imgui.h>
#include <stdexcept>

void SceneManager::loadScene(const std::shared_ptr<SceneAsset>& scene)
{
  if (!scene)
  {
    throw std::runtime_error("Attempted to load a scene that does not exist!");
  }

  resetScene();

  m_currentScene = scene;
}

void SceneManager::fixedUpdate(const float dt) const
{
  if (m_currentScene)
  {
    m_currentScene->fixedUpdate(dt);
  }
}

void SceneManager::variableUpdate()
{
  displaySceneStatusGui();

  if (m_currentScene)
  {
    m_currentScene->variableUpdate();
  }
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

void SceneManager::displaySceneStatusGui()
{
  ImGui::Begin("Scene Status");

  constexpr int sceneStatusButtonWidth = 125;

  if (m_sceneStatus != SceneStatus::running)
  {
    if (ImGui::Button("Start", {sceneStatusButtonWidth, 0}))
    {
      startScene();
    }
  }

  if (m_sceneStatus == SceneStatus::running)
  {
    if (ImGui::Button("Pause", {sceneStatusButtonWidth, 0}))
    {
      pauseScene();
    }
  }

  if (m_sceneStatus != SceneStatus::stopped)
  {
    ImGui::SameLine();

    if (ImGui::Button("Stop", {sceneStatusButtonWidth, 0}))
    {
      resetScene();
    }
  }

  ImGui::End();
}
