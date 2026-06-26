#include "SaveUI.h"
#include <ProjectSerializer.h>
#include <VulkanEngine/VulkanEngine.h>
#include <GLFW/glfw3.h>
#include <nfd.h>
#include <nlohmann/json.hpp>
#include <uuid.h>
#include <array>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <vector>

SaveUI::SaveUI(ProjectSerializer* projectSerializer, std::shared_ptr<vke::VulkanEngine> renderer)
  : m_projectSerializer(projectSerializer),
    m_renderer(std::move(renderer))
{
  registerWindowEvents();
}

SaveUI::~SaveUI()
{
  if (m_renderer)
  {
    const auto window = m_renderer->getWindow();
    window->removeListener(m_keyCallbackEventListener);
    window->removeListener(m_dropEventListener);
  }
}

void SaveUI::setLoadProjectCallback(LoadProjectCallback callback)
{
  m_onLoadProject = std::move(callback);
}

void SaveUI::setEditable(const bool editable)
{
  m_editable = editable;
}

void SaveUI::save()
{
  if (m_saveFile.empty() && !createSaveFile())
  {
    return;
  }

  // Serialize the editor's replicated project (kept current by snapshots/deltas) straight to disk.
  m_projectSerializer->save(m_saveFile);

  std::cout << "[SaveUI] Saved project to " << m_saveFile << std::endl;
}

void SaveUI::saveAs()
{
  if (createSaveFile())
  {
    save();
  }
}

void SaveUI::open()
{
  if (!chooseSaveFile())
  {
    return;
  }

  loadFromFile(m_saveFile);
}

void SaveUI::loadFromFile(const std::string& path)
{
  std::ifstream f(path);
  if (!f.is_open())
  {
    std::cerr << "[SaveUI] Could not open project file: " << path << std::endl;
    return;
  }

  std::stringstream buffer;
  buffer << f.rdbuf();
  const std::string content = buffer.str();

  m_saveFile = path;

  loadProjectBlob(content);
  std::cout << "[SaveUI] Opened project " << path << std::endl;
}

void SaveUI::loadProjectBlob(const std::string& projectJson) const
{
  const auto json = nlohmann::json::parse(projectJson, nullptr, false);
  if (json.is_discarded())
  {
    std::cerr << "[SaveUI] Project file is not valid JSON." << std::endl;
    return;
  }

  // Apply it locally for immediate feedback (the editor's replicated managers), then hand the blob to
  // the app to forward to the authoritative server, which reloads + re-snapshots to keep everyone in
  // sync. Local-only would desync; server-only leaves the editor blank if the round-trip lags.
  try
  {
    m_projectSerializer->deserialize(json);
  }
  catch (const std::exception& e)
  {
    std::cerr << "[SaveUI] Failed to load project: " << e.what() << std::endl;
    return;
  }

  if (m_onLoadProject)
  {
    m_onLoadProject(projectJson);
  }
}

void SaveUI::createNewProject()
{
  // A fresh project with a single empty scene to start editing in.
  std::mt19937 rng{ std::random_device{}() };
  uuids::uuid_random_generator generator{ rng };
  const auto sceneUUID = uuids::to_string(generator());

  const nlohmann::json project = {
    { "assets", {
      { "models", nlohmann::json::array() },
      { "textures", nlohmann::json::array() },
      { "scripts", nlohmann::json::array() },
      { "scenes", nlohmann::json::array({
        { { "name", "New Scene" }, { "uuid", sceneUUID }, { "objects", nlohmann::json::array() } }
      }) }
    } },
    { "currentSceneUUID", sceneUUID }
  };

  m_saveFile = "";
  loadProjectBlob(project.dump());
}

bool SaveUI::chooseSaveFile()
{
  if (NFD_Init() != NFD_OKAY)
  {
    std::cerr << "[SaveUI] NFD_Init failed" << std::endl;
    return false;
  }

  nfdu8char_t* outPath = nullptr;
  const std::array<nfdu8filteritem_t, 1> filters { { { "ECS3D Project Files", "json" } } };

  const nfdopendialogu8args_t args {
    .filterList = filters.data(),
    .filterCount = static_cast<nfdfiltersize_t>(filters.size())
  };

  const nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);

  if (result != NFD_OKAY)
  {
    NFD_Quit();
    return false;
  }

  m_saveFile = std::string(outPath);
  NFD_FreePathU8(outPath);
  NFD_Quit();

  return true;
}

bool SaveUI::createSaveFile()
{
  if (NFD_Init() != NFD_OKAY)
  {
    std::cerr << "[SaveUI] NFD_Init failed" << std::endl;
    return false;
  }

  nfdu8char_t* outPath = nullptr;
  const std::array<nfdu8filteritem_t, 1> filters { { { "ECS3D Project Files", "json" } } };

  const nfdsavedialogu8args_t args {
    .filterList = filters.data(),
    .filterCount = static_cast<nfdfiltersize_t>(filters.size()),
    .defaultName = "project.json"
  };

  const nfdresult_t result = NFD_SaveDialogU8_With(&outPath, &args);

  if (result != NFD_OKAY)
  {
    NFD_Quit();
    return false;
  }

  m_saveFile = std::string(outPath);
  NFD_FreePathU8(outPath);
  NFD_Quit();

  return true;
}

void SaveUI::registerWindowEvents()
{
  const auto window = m_renderer->getWindow();

  // Capture only `this` (not the window shared_ptr) to avoid a window->listener->lambda->window cycle.
  m_keyCallbackEventListener = window->on<vke::KeyCallbackEvent>([this](const vke::KeyCallbackEvent& e) {
    const auto window = m_renderer->getWindow();
    if (e.action == GLFW_PRESS && window->keyIsPressed(GLFW_KEY_LEFT_CONTROL) && window->keyIsPressed(GLFW_KEY_S))
    {
      if (window->keyIsPressed(GLFW_KEY_LEFT_SHIFT))
      {
        saveAs();
      }
      else
      {
        save();
      }
    }
  });

  m_dropEventListener = window->on<vke::DropEvent>([this](const vke::DropEvent& e) {
    if (!m_editable)
    {
      std::cout << "[SaveUI] Connect to a server in edit mode to open a project." << std::endl;
      return;
    }

    if (e.paths.size() == 1)
    {
      loadFromFile(e.paths.front());
    }
  });
}
