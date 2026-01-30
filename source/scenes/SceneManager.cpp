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
  auto scene = std::make_shared<Scene>(this);

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

  m_currentScene = scene;

  m_currentScene->load();
}

ECS3D* SceneManager::getECS() const
{
  return m_ecs;
}

void SceneManager::update(const float dt)
{
  sceneSelector();

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

void SceneManager::sceneSelector()
{
  ImGui::Begin("Scene Selector");

  if (ImGui::Button("Create New Scene", {ImGui::GetContentRegionAvail().x, 45}))
  {
    createScene();
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  for (int i = 0; i < m_scenes.size(); ++i)
  {
    if (ImGui::TreeNodeEx(("Scene " + std::to_string(i + 1)).c_str(),
                          ImGuiTreeNodeFlags_Leaf |
                          (m_currentScene == m_scenes[i] ? ImGuiTreeNodeFlags_Selected : 0) |
                          ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth))
    {
      if (ImGui::IsItemClicked())
      {
        loadScene(m_scenes[i]);
      }

      ImGui::TreePop();
    }
  }

  ImGui::End();
}
