#include "SaveUI.h"
#include "GuiComponents.h"
#include <ProjectSerializer.h>
#include <VulkanEngine/VulkanEngine.h>
#include <imgui.h>
#include <nfd.h>
#include <nlohmann/json.hpp>
#include <uuid.h>
#include <array>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <vector>

namespace {
  // GLFW's close callback is a plain function pointer with no room for a captured `this`, and only one
  // SaveUI (one window) exists per editor process, so a single back-pointer is enough to reach it.
  SaveUI* s_activeSaveUI = nullptr;
}

SaveUI::SaveUI(ProjectSerializer* projectSerializer, std::shared_ptr<vke::VulkanEngine> renderer)
  : m_projectSerializer(projectSerializer),
    m_renderer(std::move(renderer))
{
  s_activeSaveUI = this;

  registerWindowEvents();
}

SaveUI::~SaveUI()
{
  if (s_activeSaveUI == this)
  {
    s_activeSaveUI = nullptr;
  }

  if (m_renderer)
  {
    m_renderer->getWindow()->removeListener(m_dropEventListener);
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

bool SaveUI::save()
{
  if (m_saveFile.empty() && !createSaveFile())
  {
    return false;
  }

  // Serialize the editor's replicated project (kept current by snapshots/deltas) straight to disk.
  if (!m_projectSerializer->save(m_saveFile))
  {
    return false;
  }

  m_savedEditCount = m_editCount;

  std::cout << "[SaveUI] Saved project to " << m_saveFile << std::endl;
  return true;
}

void SaveUI::saveAs()
{
  if (createSaveFile())
  {
    static_cast<void>(save());
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

void SaveUI::requestNewProject()
{
  guardDiscard(PendingDiscard::newProject);
}

void SaveUI::requestOpen()
{
  guardDiscard(PendingDiscard::open);
}

void SaveUI::markEdited()
{
  ++m_editCount;
}

bool SaveUI::isDirty() const
{
  return m_editCount != m_savedEditCount;
}

void SaveUI::loadFromFile(const std::string& path)
{
  const std::ifstream f(path);
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

void SaveUI::loadProjectBlob(const std::string& projectJson)
{
  const auto json = nlohmann::json::parse(projectJson, nullptr, false);
  if (json.is_discarded())
  {
    std::cerr << "[SaveUI] Project file is not valid JSON." << std::endl;
    return;
  }

  // Apply it locally for immediate feedback (the editor's replicated managers), then notify the app to
  // forward it to the authoritative server, which reloads + re-snapshots to keep everyone in sync.
  // Local-only would desync; server-only leaves the editor blank if the round-trip lags.
  try
  {
    m_projectSerializer->deserialize(json);
  }
  catch (const std::exception& e)
  {
    std::cerr << "[SaveUI] Failed to load project: " << e.what() << std::endl;
    return;
  }

  // A freshly loaded project has nothing unsaved yet.
  m_savedEditCount = m_editCount;

  if (m_onLoadProject)
  {
    m_onLoadProject();
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

  m_dropEventListener = window->on<vke::DropEvent>([this](const vke::DropEvent& e) {
    if (!m_editable)
    {
      std::cout << "[SaveUI] Connect to a server in edit mode to open a project." << std::endl;
      return;
    }

    if (e.paths.size() == 1)
    {
      guardDiscard(PendingDiscard::loadFile, e.paths.front());
    }
  });

  // vke sets no close callback of its own (see Window.h), so this is free to claim. GLFW callbacks can't
  // capture state, hence the s_activeSaveUI back-pointer above.
  glfwSetWindowCloseCallback(window->getWindow(), windowCloseCallback);
}

void SaveUI::guardDiscard(const PendingDiscard action, std::string path)
{
  if (!isDirty())
  {
    performDiscard(action, path);
    return;
  }

  m_pendingDiscard = action;
  m_pendingLoadPath = std::move(path);
  m_showUnsavedChangesModal = true;
}

void SaveUI::performDiscard(const PendingDiscard action, const std::string& path)
{
  switch (action)
  {
    case PendingDiscard::newProject:
      createNewProject();
      break;

    case PendingDiscard::open:
      open();
      break;

    case PendingDiscard::loadFile:
      loadFromFile(path);
      break;

    case PendingDiscard::closeWindow:
      // Re-affirm the close the window-close callback vetoed; now that it's resolved, let GLFW proceed.
      glfwSetWindowShouldClose(m_renderer->getWindow()->getWindow(), true);
      break;

    case PendingDiscard::none:
      break;
  }
}

void SaveUI::windowCloseCallback(GLFWwindow* window)
{
  if (!s_activeSaveUI || !s_activeSaveUI->isDirty())
  {
    // Nothing to lose - let the close proceed as GLFW already intends.
    return;
  }

  // Veto it here; guardDiscard re-affirms the close (PendingDiscard::closeWindow, above) once the
  // unsaved-changes prompt resolves, or leaves it vetoed on Cancel.
  glfwSetWindowShouldClose(window, false);

  s_activeSaveUI->guardDiscard(PendingDiscard::closeWindow);
}

void SaveUI::displayUnsavedChangesModal()
{
  if (!m_showUnsavedChangesModal)
  {
    return;
  }

  ImGui::OpenPopup("Unsaved Changes");

  if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::TextUnformatted("This project has unsaved changes.");
    ImGui::TextColored(theme::t3, "Save them before continuing?");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Save", ImVec2(100, 0)))
    {
      m_showUnsavedChangesModal = false;
      ImGui::CloseCurrentPopup();

      // Only proceed with the pending action if the save actually landed; a canceled Save As (no path
      // yet) or a write failure aborts the whole request, same as Cancel.
      if (save())
      {
        const auto action = m_pendingDiscard;
        m_pendingDiscard = PendingDiscard::none;
        performDiscard(action, m_pendingLoadPath);
      }
      else
      {
        m_pendingDiscard = PendingDiscard::none;
      }
    }

    ImGui::SameLine();

    if (ImGui::Button("Don't Save", ImVec2(100, 0)))
    {
      m_showUnsavedChangesModal = false;
      ImGui::CloseCurrentPopup();

      const auto action = m_pendingDiscard;
      m_pendingDiscard = PendingDiscard::none;
      performDiscard(action, m_pendingLoadPath);
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
      m_showUnsavedChangesModal = false;
      m_pendingDiscard = PendingDiscard::none;
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }
}
