#include "SceneManager.h"
#include "Scene.h"
#include "../ECS3D.h"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <stdexcept>

SceneManager::SceneManager(ECS3D* ecs)
  : m_ecs(ecs)
{}

std::shared_ptr<Scene> SceneManager::createScene()
{
  auto scene = std::make_shared<Scene>(this, "Scene " + std::to_string(findValidSceneIndex()));

  m_scenes.push_back(scene);

  if (!m_currentScene)
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

  resetScene();

  m_currentScene = scene;
}

ECS3D* SceneManager::getECS() const
{
  return m_ecs;
}

void SceneManager::update(const float dt)
{
  displaySceneStatusGui();

  sceneSelector();

  deleteScenesToRemove();

  if (!m_currentScene)
  {
    return;
  }

  m_currentScene->update(dt);
}

nlohmann::json SceneManager::serialize() const
{
  nlohmann::json data = nlohmann::json::array();

  for (const auto& scene : m_scenes)
  {
    data.push_back(scene->serialize());
  }

  return data;
}

void SceneManager::loadFromJSON(const nlohmann::json& scenesData)
{
  for (const auto& sceneData : scenesData)
  {
    const auto scene = createScene();

    scene->loadFromJSON(sceneData);
  }
}

std::shared_ptr<Scene> SceneManager::getCurrentScene() const
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

void SceneManager::sceneSelector()
{
  ImGui::Begin("Scenes");

  if (ImGui::Button("Create New Scene", {ImGui::GetContentRegionAvail().x, 45}))
  {
    createScene();
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  for (const auto& scene : m_scenes)
  {
    if (ImGui::TreeNodeEx(scene->getName().c_str(),
                          ImGuiTreeNodeFlags_Leaf |
                          (m_currentScene == scene ? ImGuiTreeNodeFlags_Selected : 0) |
                          ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap))
    {
      if (ImGui::IsItemClicked())
      {
        loadScene(scene);
      }

      displayRemoveSceneButton(scene);

      ImGui::TreePop();
    }
  }

  ImGui::End();
}

void SceneManager::displayRemoveSceneButton(const std::shared_ptr<Scene>& scene)
{
  ImGui::SameLine();

  const float buttonWidth = ImGui::CalcTextSize("-").x + ImGui::GetStyle().FramePadding.x * 4.0f;
  const float contentRegionWidth = ImGui::GetContentRegionAvail().x;

  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + contentRegionWidth - buttonWidth);

  if (ImGui::Button("-", {buttonWidth, 0}))
  {
    ImGui::OpenPopup("Delete Scene?");
  }

  if (ImGui::BeginPopupModal("Delete Scene?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::Text("Are you sure you want to delete");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "%s", scene->getName().c_str());
    ImGui::SameLine();
    ImGui::Text("?");

    ImGui::Text("This action cannot be undone.");

    ImGui::Separator();

    if (ImGui::Button("Yes", ImVec2(120, 0)))
    {
      m_scenesToRemove.push_back(scene);
      ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();

    if (ImGui::Button("No", ImVec2(120, 0)))
    {
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }
}

void SceneManager::deleteScenesToRemove()
{
  if (m_scenesToRemove.empty())
  {
    return;
  }

  for (const auto& scene : m_scenesToRemove)
  {
    if (scene == m_currentScene)
    {
      m_currentScene = nullptr;
    }

    std::erase(m_scenes, scene);
  }

  m_scenesToRemove.clear();
}

uint32_t SceneManager::findValidSceneIndex() const
{
  uint32_t sceneIndex = m_scenes.size();

  bool validIndex = false;
  while (!validIndex)
  {
    validIndex = true;
    sceneIndex++;

    for (const auto& scene : m_scenes)
    {
      if (scene->getName() == "Scene " + std::to_string(sceneIndex))
      {
        validIndex = false;
        break;
      }
    }
  }

  return sceneIndex;
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
