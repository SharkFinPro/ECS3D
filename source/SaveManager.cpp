#include "SaveManager.h"
#include "ECS3D.h"
#include "assets/AssetManager.h"
#include "scenes/SceneManager.h"
#include <GLFW/glfw3.h>
#include <nfd.h>
#include <VulkanEngine/components/window/Window.h>
#include <fstream>
#include <iostream>

SaveManager::SaveManager(ECS3D* ecs)
  : m_ecs(ecs)
{
  registerWindowEvents();
}

void SaveManager::save()
{
  if (m_saveFile.empty() && !createSaveFile())
  {
    return;
  }

  const nlohmann::json data = {
    { "assets", m_ecs->getAssetManager()->serialize() },
    { "scenes", m_ecs->getSceneManager()->serialize() }
  };

  std::ofstream outFile(m_saveFile);
  outFile << data.dump(2);
  outFile.close();

  std::cout << "Saved data to " << m_saveFile << std::endl;
}

void SaveManager::saveAs()
{
  if (createSaveFile())
  {
    save();
  }
}

void SaveManager::loadSaveFile(const std::string& saveFile)
{
  m_saveFile = saveFile;

  loadFromSaveFile();
}

void SaveManager::loadSaveFile()
{
  if (!chooseSaveFile())
  {
    return;
  }

  loadFromSaveFile();
}

nlohmann::json SaveManager::readSaveDataFile() const
{
  std::ifstream f(m_saveFile);

  if (!f.is_open())
  {
    f.close();

    return nlohmann::json{};
  }

  nlohmann::json saveData{};

  try
  {
    saveData = nlohmann::json::parse(f);
  }
  catch ([[maybe_unused]] const std::exception& e)
  {
    f.close();

    return nlohmann::json{};
  }

  f.close();

  return saveData;
}

bool SaveManager::chooseSaveFile()
{
  if (NFD_Init() != NFD_OKAY)
  {
    throw std::runtime_error("NFD_Init failed");
  }

  nfdu8char_t* outPath = nullptr;
  const std::vector<nfdu8filteritem_t> filters {
    { "ECS3D Project Files", "json" }
  };

  const nfdopendialogu8args_t args {
    .filterList = filters.data(),
    .filterCount = static_cast<nfdfiltersize_t>(filters.size())
  };

  const nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);

  if (result == NFD_ERROR)
  {
    NFD_Quit();
    throw std::runtime_error("NFD_OpenDialogU8_With failed");
  }

  if (result == NFD_CANCEL)
  {
    NFD_Quit();
    return false;
  }

  m_saveFile = std::string(outPath);
  NFD_FreePathU8(outPath);
  NFD_Quit();

  return true;
}

bool SaveManager::createSaveFile()
{
  if (NFD_Init() != NFD_OKAY)
  {
    throw std::runtime_error("NFD_Init failed");
  }

  nfdu8char_t* outPath = nullptr;
  const std::vector<nfdu8filteritem_t> filters {
    { "ECS3D Project Files", "json" }
  };

  const nfdsavedialogu8args_t args {
    .filterList = filters.data(),
    .filterCount = static_cast<nfdfiltersize_t>(filters.size()),
    .defaultName = "project.json"
  };

  const nfdresult_t result = NFD_SaveDialogU8_With(&outPath, &args);

  if (result == NFD_ERROR)
  {
    NFD_Quit();
    throw std::runtime_error("NFD_SaveDialogU8_With failed");
  }

  if (result == NFD_CANCEL)
  {
    NFD_Quit();
    return false;
  }

  m_saveFile = std::string(outPath);
  NFD_FreePathU8(outPath);
  NFD_Quit();

  return true;
}

void SaveManager::registerWindowEvents()
{
  m_ecs->getRenderer()->getWindow()->on<vke::KeyCallbackEvent>([this](const vke::KeyCallbackEvent& e) {
    if (e.action == GLFW_PRESS && m_ecs->keyIsPressed(GLFW_KEY_LEFT_CONTROL) && m_ecs->keyIsPressed(GLFW_KEY_S))
    {
      if (m_ecs->keyIsPressed(GLFW_KEY_LEFT_SHIFT))
      {
        saveAs();
      }
      else
      {
        save();
      }
    }
  });

  m_ecs->getRenderer()->getWindow()->on<vke::DropEvent>([this](const vke::DropEvent& e) {
    if (e.paths.size() == 1)
    {
      loadSaveFile(e.paths.front());
    }
  });
}

void SaveManager::loadFromSaveFile()
{
  const auto saveData = readSaveDataFile();

  if (saveData.empty())
  {
    return;
  }

  m_ecs->prepareForReset();

  try
  {
    m_ecs->getAssetManager()->loadFromJSON(saveData.at("assets"));

    m_ecs->getSceneManager()->loadFromJSON(saveData.at("scenes"));

    m_ecs->completeReset();
  } catch (const std::exception& e)
  {
    m_ecs->logMessage("Error", e.what());
    m_ecs->logMessage("Error", "Failed to load save file: " + m_saveFile);

    m_saveFile = "";

    m_ecs->cancelReset();
  }
}
