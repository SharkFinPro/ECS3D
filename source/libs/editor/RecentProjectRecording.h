#ifndef RECENTPROJECTRECORDING_H
#define RECENTPROJECTRECORDING_H

#include <filesystem>
#include <optional>

// What the editor adds to the shared recent-projects list after a successful open or save: the project file
// or folder, as given. RecentProjects folds a project.ecs3d into its folder and keeps a legacy .json as the
// file. An untitled template and a project with no file yet are never recorded.
[[nodiscard]] inline std::optional<std::filesystem::path> recentProjectToRecord(
  const std::filesystem::path& projectPath, const bool isUntitledTemplate)
{
  if (isUntitledTemplate || projectPath.empty())
  {
    return std::nullopt;
  }

  return projectPath;
}

#endif //RECENTPROJECTRECORDING_H
