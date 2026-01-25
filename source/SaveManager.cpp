#include "SaveManager.h"
#include "ECS3D.h"
#include "assets/AssetManager.h"
#include "scenes/SceneManager.h"
#include <fstream>
#include <iostream>

const std::string FILE_NAME = "ECSData3.json";

SaveManager::SaveManager(ECS3D* ecs)
  : m_ecs(ecs)
{
  readSaveDataFile();
}

void SaveManager::save() const
{
  nlohmann::json data = {
    {"prefabs", {
        {"objects", nlohmann::json::array()}
    }}
  };

  auto serializedAssetManager = m_ecs->getAssetManager()->serialize();
  data["assets"] = serializedAssetManager["assets"];

  auto serializedSceneManager = m_ecs->getSceneManager()->serialize();
  data["scenes"] = serializedSceneManager["scenes"];


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
  std::cout << m_saveData["prefabs"]["objects"].size() << " object prefab(s) found" << std::endl;
  std::cout << m_saveData["scenes"].size() << " scene(s) found" << std::endl;

  std::cout << m_saveData << std::endl;
}

void SaveManager::createSaveDataFile()
{
  const nlohmann::json defaultData = {
    {"scenes", nlohmann::json::array()},
    {"prefabs", {
      {"objects", nlohmann::json::array()}
    }},
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
