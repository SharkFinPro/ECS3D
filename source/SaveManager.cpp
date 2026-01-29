#include "SaveManager.h"
#include "ECS3D.h"
#include "assets/AssetManager.h"
#include "scenes/SceneManager.h"
#include <GLFW/glfw3.h>
#include <VulkanEngine/components/window/Window.h>
#include <fstream>
#include <iostream>

SaveManager::SaveManager(ECS3D* ecs,
                         std::string saveFile)
  : m_ecs(ecs), m_saveFile(std::move(saveFile))
{
  const auto saveData = readSaveDataFile();

  if (saveData.empty())
  {
    return;
  }

  m_ecs->getAssetManager()->loadFromJSON(saveData["assets"]);

  m_ecs->getSceneManager()->loadFromJSON(saveData["scenes"]);

  m_ecs->getRenderer()->getWindow()->on<vke::KeyCallbackEvent>([this](const vke::KeyCallbackEvent& e) {
    if (e.action == GLFW_PRESS && m_ecs->keyIsPressed(GLFW_KEY_LEFT_CONTROL) && m_ecs->keyIsPressed(GLFW_KEY_S))
    {
      save();
    }
  });
}

void SaveManager::save() const
{
  const nlohmann::json data = {
    { "assets", m_ecs->getAssetManager()->serialize() },
    { "scenes", m_ecs->getSceneManager()->serialize() }
  };

  std::ofstream outFile(m_saveFile);
  outFile << data.dump(2);
  outFile.close();

  std::cout << "Saved data to " << m_saveFile << std::endl;
}

nlohmann::json SaveManager::readSaveDataFile() const
{
  std::ifstream f(m_saveFile);

  if (!f.is_open())
  {
    f.close();

    return nlohmann::json{};
  }

  nlohmann::json saveData = nlohmann::json::parse(f);

  f.close();

  return saveData;
}
