#ifndef PROJECTORIGIN_H
#define PROJECTORIGIN_H

#include "ProjectPaths.h"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

// A template is a project the editor must never write into: the built-in default project (no project file
// at all) or a folder holding a template.json. A legacy bare .json project has a file but no root and is
// an ordinary project.
[[nodiscard]] bool isTemplateProject(const ProjectLocation& location);

// The name to show for a template: the manifest's name, else the folder's name, else none (built-in).
[[nodiscard]] std::optional<std::string> templateDisplayName(const ProjectLocation& location);

// "Untitled (from <name>)", or "Untitled (from the built-in project)" without a name.
[[nodiscard]] std::string untitledTitle(const std::optional<std::string>& templateName);

// <Documents>/ECS3D Projects. Empty when the environment names no home directory.
[[nodiscard]] std::filesystem::path defaultProjectsDirectory();

// A folder a new project can be created in: it does not exist, or exists as an empty directory.
[[nodiscard]] bool folderIsFreeForNewProject(const std::filesystem::path& folder);

// The first of "<name>", "<name> 2", "<name> 3", ... under projectsDir that isTaken does not report.
// Characters a folder name cannot hold are dropped from templateName; nothing left becomes "New Project".
[[nodiscard]] std::filesystem::path defaultSaveFolder(const std::filesystem::path& projectsDir,
                                                      const std::string& templateName,
                                                      const std::function<bool(const std::filesystem::path&)>& isTaken);

// The same, taking a folder as free when folderIsFreeForNewProject says so.
[[nodiscard]] std::filesystem::path defaultSaveFolder(const std::filesystem::path& projectsDir,
                                                      const std::string& templateName);

// Why a template may not be saved into folder (it is not free, or it is or lies inside the template's own
// folder), or nullopt when it may.
[[nodiscard]] std::optional<std::string> templateSaveRefusal(const std::filesystem::path& folder,
                                                             const std::filesystem::path& templateRoot);

// Save writes in place unless the project is a template that has not yet been saved to a folder of its own.
[[nodiscard]] constexpr bool saveActsAsSaveAs(const bool isTemplate, const bool savedToFolderSinceOpen)
{
  return isTemplate && !savedToFolderSinceOpen;
}

#endif //PROJECTORIGIN_H
