#include "SaveUI.h"
#include "GuiComponents.h"
#include <Log.h>
#include <ProjectOrigin.h>
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
  m_saveFile = location.projectFile;
  m_projectRoot = location.root;
  setOrigin(location);
}

void SaveUI::setOrigin(const ProjectLocation& location)
{
  m_isTemplate = isTemplateProject(location);
  m_templateName = m_isTemplate ? templateDisplayName(location) : std::nullopt;
  m_savedToFolderSinceOpen = false;
  updateWindowTitle();
}

void SaveUI::updateWindowTitle()
{
  const std::string base = "ECS3D Editor";
  const auto title = saveActsAsSaveAs(m_isTemplate, m_savedToFolderSinceOpen)
    ? untitledTitle(m_templateName) + " - " + base
    : base;

  glfwSetWindowTitle(m_renderer->getWindow()->getWindow(), title.c_str());
}

void SaveUI::setEditable(const bool editable)
{
  m_editable = editable;
}

bool SaveUI::save()
{
  if (m_saveFile.empty() || saveActsAsSaveAs(m_isTemplate, m_savedToFolderSinceOpen))
  {
    return saveAs();
  }

  return writeProject(m_saveFile);
}

bool SaveUI::isUntitledTemplate() const
{
  return assetWritesBlocked(m_isTemplate, m_savedToFolderSinceOpen);
}

bool SaveUI::blockAssetWrites()
{
  if (!isUntitledTemplate())
  {
    return false;
  }

  Log::info(LogCategory::editor, "Save the project to a folder first - templates are never modified.");
  static_cast<void>(saveAs());
  return true;
}

bool SaveUI::saveAs()
{
  std::filesystem::path folder;
  std::filesystem::path startIn;
  std::string defaultName;

  if (saveActsAsSaveAs(m_isTemplate, m_savedToFolderSinceOpen))
  {
    startIn = defaultProjectsDirectory();
    if (!startIn.empty())
    {
      // The dialog only starts in a folder that exists, so make it when missing.
      std::error_code error;
      if (!std::filesystem::exists(startIn, error))
      {
        std::filesystem::create_directories(startIn, error);
      }
      defaultName = pathToUtf8(defaultSaveFolder(startIn, m_templateName.value_or("New Project")).filename());
    }
  }

  return chooseSaveFolder(folder, startIn, defaultName) && saveToFolder(folder);
}

bool SaveUI::writeProject(const std::filesystem::path& file)
{
  // Serialize the editor's replicated project (kept current by snapshots/deltas) straight to disk.
  if (!m_projectSerializer->save(file))
  {
    Log::error(LogCategory::editor, "Failed to save project to " + pathToUtf8(file));
    return false;
  }

  m_unsavedChanges.markSaved();

  Log::info(LogCategory::editor, "Saved project to " + pathToUtf8(file));
  return true;
}

bool SaveUI::saveToFolder(const std::filesystem::path& folder)
{
  if (saveActsAsSaveAs(m_isTemplate, m_savedToFolderSinceOpen))
  {
    if (const auto refusal = templateSaveRefusal(folder, m_projectRoot))
    {
      Log::error(LogCategory::editor, "Not saving: " + pathToUtf8(folder) + " cannot take a new project: " +
        *refusal + ". Choose a new or empty folder.");
      return false;
    }
  }
  else if (saveFolderHoldsOtherProject(folder, m_projectRoot))
  {
    Log::error(LogCategory::editor, "Not saving: " + pathToUtf8(folder) + " already holds a project. " +
      "Choose an empty folder, or open that project to edit it.");
    return false;
  }

  std::error_code error;
  if (!ensureProjectFolders(folder, error))
  {
    Log::error(LogCategory::editor, "Could not create the project folder " + pathToUtf8(folder) + ": " +
      error.message());
    return false;
  }

  copyProjectContent(m_projectRoot, folder, error);
  if (error)
  {
    Log::error(LogCategory::editor, "Could not copy the project's assets and scripts to " + pathToUtf8(folder) + ": " +
      error.message());
    return false;
  }

  const auto file = projectFileInFolder(folder);
  if (!writeProject(file))
  {
    return false;
  }

  m_saveFile = file;

  if (folder != m_projectRoot)
  {
    setProjectRoot(folder, false);
  }

  m_isTemplate = false;
  m_savedToFolderSinceOpen = true;
  updateWindowTitle();

  return true;
}

void SaveUI::setProjectRoot(const std::filesystem::path& root, const bool reopened)
{
  m_projectRoot = root;

  if (m_onProjectRoot)
  {
    m_onProjectRoot(root, reopened);
  }
}

void SaveUI::open()
{
  std::filesystem::path file;
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

void SaveUI::loadFromFile(const std::filesystem::path& path)
{
  const auto location = locateProject(path);

  const std::ifstream f(location.projectFile);
  if (!f.is_open())
  {
    Log::error(LogCategory::editor, "Could not open project file: " + pathToUtf8(location.projectFile));
    return;
  }

  std::stringstream buffer;
  buffer << f.rdbuf();

  if (!loadProjectBlob(buffer.str(), location.projectFile, location.root))
  {
    return;
  }

  Log::info(LogCategory::editor, "Opened project " + pathToUtf8(location.projectFile));

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

  m_saveFile = projectFile;
  setOrigin({ projectFile, root });

  // Told on every load, even for the same folder: the project's asset records may differ. Before the load
  // callback, so the asset cache resolves against the new folder from the first frame.
  setProjectRoot(root, true);

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

  if (loadProjectBlob(project.dump(), {}, {}))
  {
    m_isTemplate = false;
    m_templateName.reset();
    updateWindowTitle();
  }
}

bool SaveUI::chooseProjectFile(std::filesystem::path& file)
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

  file = pathFromUtf8(outPath);
  NFD_FreePathU8(outPath);
  NFD_Quit();

  return true;
}

bool SaveUI::chooseSaveFolder(std::filesystem::path& folder, const std::filesystem::path& startIn,
                              const std::string& defaultName)
{
  if (NFD_Init() != NFD_OKAY)
  {
    Log::error(LogCategory::editor, "NFD_Init failed");
    return false;
  }

  nfdu8char_t* outPath = nullptr;

  const std::string start = pathToUtf8(startIn);
  const char* startPath = start.empty() ? nullptr : start.c_str();

  nfdresult_t result;
  if (defaultName.empty())
  {
    result = NFD_PickFolderU8(&outPath, startPath);
  }
  else
  {
    const nfdsavedialogu8args_t args {
      .defaultPath = startPath,
      .defaultName = defaultName.c_str()
    };
    result = NFD_SaveDialogU8_With(&outPath, &args);
  }

  if (result != NFD_OKAY)
  {
    NFD_Quit();
    return false;
  }

  folder = pathFromUtf8(outPath);
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
      guardDiscard(PendingDiscard::loadFile, pathFromUtf8(e.paths.front().c_str()));
    }
  });

  // vke sets no close callback of its own (see Window.h), so this is free to claim. GLFW callbacks can't
  // capture state, hence the s_activeSaveUI back-pointer above.
  glfwSetWindowCloseCallback(window->getWindow(), windowCloseCallback);
}

void SaveUI::guardDiscard(const PendingDiscard action, std::filesystem::path path)
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

void SaveUI::performDiscard(const PendingDiscard action, const std::filesystem::path& path)
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
