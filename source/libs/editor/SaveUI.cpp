#include "SaveUI.h"
#include "GuiComponents.h"
#include <Log.h>
#include <ProjectPaths.h>
#include <ProjectSerializer.h>
#include <VulkanEngine/VulkanEngine.h>
#include <imgui.h>
#include <nfd.h>
#include <nlohmann/json.hpp>
#include <uuid.h>
#include <array>
#include <fstream>
#include <random>
#include <system_error>
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

void SaveUI::setProjectRootCallback(ProjectRootCallback callback)
{
  m_onProjectRoot = std::move(callback);
}

void SaveUI::setStartupProject(const std::filesystem::path& argument)
{
  const auto location = locateProject(argument);
  m_saveFile = location.projectFile.string();
  m_projectRoot = location.root;
}

void SaveUI::setEditable(const bool editable)
{
  m_editable = editable;
}

bool SaveUI::save()
{
  if (m_saveFile.empty())
  {
    return saveAs();
  }

  return writeProject(m_saveFile);
}

bool SaveUI::saveAs()
{
  std::filesystem::path folder;
  return chooseSaveFolder(folder) && saveToFolder(folder);
}

bool SaveUI::writeProject(const std::string& file)
{
  // Serialize the editor's replicated project (kept current by snapshots/deltas) straight to disk.
  if (!m_projectSerializer->save(file))
  {
    Log::error(LogCategory::editor, "Failed to save project to " + file);
    return false;
  }

  m_unsavedChanges.markSaved();

  Log::info(LogCategory::editor, "Saved project to " + file);
  return true;
}

bool SaveUI::saveToFolder(const std::filesystem::path& folder)
{
  std::error_code error;
  if (!ensureProjectFolders(folder, error))
  {
    Log::error(LogCategory::editor, "Could not create the project folder " + folder.string() + ": " + error.message());
    return false;
  }

  copyProjectContent(m_projectRoot, folder, error);
  if (error)
  {
    Log::error(LogCategory::editor, "Could not copy the project's assets and scripts to " + folder.string() + ": " +
      error.message());
    return false;
  }

  const auto file = projectFileInFolder(folder).string();
  if (!writeProject(file))
  {
    return false;
  }

  m_saveFile = file;

  if (folder != m_projectRoot)
  {
    setProjectRoot(folder);
  }

  return true;
}

void SaveUI::setProjectRoot(const std::filesystem::path& root)
{
  m_projectRoot = root;

  if (m_onProjectRoot)
  {
    m_onProjectRoot(root);
  }
}

void SaveUI::open()
{
  std::string file;
  if (chooseProjectFile(file))
  {
    loadFromFile(file);
  }
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
  m_unsavedChanges.markEdited();
}

bool SaveUI::isDirty() const
{
  return m_unsavedChanges.isDirty();
}

void SaveUI::loadFromFile(const std::string& path)
{
  const auto location = locateProject(path);

  const std::ifstream f(location.projectFile);
  if (!f.is_open())
  {
    Log::error(LogCategory::editor, "Could not open project file: " + location.projectFile.string());
    return;
  }

  std::stringstream buffer;
  buffer << f.rdbuf();

  if (!loadProjectBlob(buffer.str(), location.projectFile, location.root))
  {
    return;
  }

  Log::info(LogCategory::editor, "Opened project " + location.projectFile.string());

  if (isLegacyProjectFile(location.projectFile))
  {
    Log::info(LogCategory::editor,
      "This is a legacy project file: Save writes it in place, and Save As moves it into a project folder.");
  }
}

bool SaveUI::loadProjectBlob(const std::string& projectJson, const std::filesystem::path& projectFile,
                             const std::filesystem::path& root)
{
  const auto json = nlohmann::json::parse(projectJson, nullptr, false);
  if (json.is_discarded())
  {
    Log::error(LogCategory::editor, "Project file is not valid JSON.");
    return false;
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
    Log::error(LogCategory::editor, std::string("Failed to load project: ") + e.what());
    return false;
  }

  m_saveFile = projectFile.string();

  // Told on every load, even for the same folder: the project's asset records may differ. Before the load
  // callback, so the asset cache resolves against the new folder from the first frame.
  setProjectRoot(root);

  // A freshly loaded project has nothing unsaved yet.
  m_unsavedChanges.markSaved();

  if (m_onLoadProject)
  {
    m_onLoadProject();
  }

  return true;
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

  static_cast<void>(loadProjectBlob(project.dump(), {}, {}));
}

bool SaveUI::chooseProjectFile(std::string& file)
{
  if (NFD_Init() != NFD_OKAY)
  {
    Log::error(LogCategory::editor, "NFD_Init failed");
    return false;
  }

  nfdu8char_t* outPath = nullptr;
  const std::array<nfdu8filteritem_t, 1> filters { { { "ECS3D Project Files", "ecs3d,json" } } };

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

  file = std::string(outPath);
  NFD_FreePathU8(outPath);
  NFD_Quit();

  return true;
}

bool SaveUI::chooseSaveFolder(std::filesystem::path& folder)
{
  if (NFD_Init() != NFD_OKAY)
  {
    Log::error(LogCategory::editor, "NFD_Init failed");
    return false;
  }

  nfdu8char_t* outPath = nullptr;

  if (NFD_PickFolderU8(&outPath, nullptr) != NFD_OKAY)
  {
    NFD_Quit();
    return false;
  }

  folder = std::filesystem::path(outPath);
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
      Log::info(LogCategory::editor, "Connect to a server in edit mode to open a project.");
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
  // A drag-and-drop drop fires straight from GLFW and isn't blocked by the modal the way menu items are.
  switch (decideDiscard(isDirty(), m_showUnsavedChangesModal))
  {
    case DiscardDecision::performNow:
      performDiscard(action, path);
      return;

    case DiscardDecision::ignore:
      Log::info(LogCategory::editor,
        "Already waiting on an unsaved-changes prompt; ignoring another discard request.");
      return;

    case DiscardDecision::prompt:
      break;
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

    // No Escape shortcut here: vke::Window::update() quits on Escape held regardless of this modal, so
    // binding Escape to Cancel would make it read as "dismiss the prompt" while it actually closes the app.
    if (ImGui::Button("Cancel", ImVec2(100, 0)))
    {
      m_showUnsavedChangesModal = false;
      m_pendingDiscard = PendingDiscard::none;
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }
}
