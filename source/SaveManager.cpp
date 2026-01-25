#include "SaveManager.h"
#include "ECS3D.h"
#include "assets/AssetManager.h"
#include "scenes/SceneManager.h"
#include <GLFW/glfw3.h>
#include <fstream>
#include <iostream>

const std::string FILE_NAME = "ECSData3.json";

SaveManager::SaveManager(ECS3D* ecs)
  : m_ecs(ecs)
{
  readSaveDataFile();

  m_ecs->getSceneManager()->loadFromJSON(m_saveData["scenes"]);
}

void SaveManager::update()
{
  const auto shouldSave = m_ecs->keyIsPressed(GLFW_KEY_LEFT_CONTROL) && m_ecs->keyIsPressed(GLFW_KEY_S);

  if (!shouldSave && m_wasSaving)
  {
    m_wasSaving = false;
  }

  if (!shouldSave || m_wasSaving)
  {
    return;
  }

  m_wasSaving = true;

  save();

  std::cout << "Saved data to " << FILE_NAME << std::endl;
}

void SaveManager::save() const
{
  const nlohmann::json data = {
    { "assets", m_ecs->getAssetManager()->serialize() },
    { "scenes", m_ecs->getSceneManager()->serialize() }
  };

  std::ofstream outFile(FILE_NAME);
  outFile << data.dump(2);
  outFile.close();
}

void SaveManager::readSaveDataFile()
{
  std::ifstream f(FILE_NAME);

  if (!f.is_open())
  {
    f.close();
    createSaveDataFile();

    f = std::ifstream(FILE_NAME);
  }

  m_saveData = nlohmann::json::parse(f);

  f.close();

  std::cout << m_saveData["assets"]["models"].size() << " model asset(s) found" << std::endl;
  std::cout << m_saveData["assets"]["textures"].size() << " texture asset(s) found" << std::endl;
  std::cout << m_saveData["scenes"].size() << " scene(s) found" << std::endl;
}

void SaveManager::createSaveDataFile()
{
  const nlohmann::json defaultData = {
    {"scenes", nlohmann::json::array()},
    {"assets", {
      {"models", nlohmann::json::array()},
      {"textures", nlohmann::json::array()}
    }}
  };

  std::ofstream outFile(FILE_NAME);
  if (outFile.is_open())
  {
    outFile << defaultData.dump(2);
    outFile.close();
    std::cout << "Created default " << FILE_NAME << " file" << std::endl;
  }
  else
  {
    std::cerr << "Failed to create " << FILE_NAME << " file" << std::endl;
  }
}
