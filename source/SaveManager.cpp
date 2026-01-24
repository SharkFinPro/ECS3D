#include "SaveManager.h"
#include <fstream>
#include <iostream>

SaveManager::SaveManager()
{
  readSaveData();
}

void SaveManager::readSaveData()
{
  std::ifstream f("ECSData.json");

  m_saveData = nlohmann::json::parse(f);

  f.close();

  std::cout << m_saveData << std::endl;
}
