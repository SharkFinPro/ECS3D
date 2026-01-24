#include "SaveManager.h"
#include <fstream>
#include <iostream>

const std::string FILE_NAME = "ECSDatas.json";

SaveManager::SaveManager()
{
  readSaveDataFile();
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
    outFile << defaultData.dump(2); // Pretty print with 2-space indent
    outFile.close();
    std::cout << "Created default " << FILE_NAME << " file" << std::endl;
  }
  else
  {
    std::cerr << "Failed to create " << FILE_NAME << " file" << std::endl;
  }
}
