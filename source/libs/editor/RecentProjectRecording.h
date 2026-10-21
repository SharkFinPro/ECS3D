#ifndef RECENTPROJECTRECORDING_H
#define RECENTPROJECTRECORDING_H

#include <ProjectOrigin.h>
#include <ProjectPaths.h>
#include <filesystem>
#include <optional>

// What the editor adds to the shared recent-projects list after a successful open or save: the project file,
// which RecentProjects then normalizes (a project.ecs3d becomes its folder, a legacy .json stays the file).
// Nothing for a project with no file yet, or for a template not yet saved into a folder of its own.
[[nodiscard]] inline std::optional<std::filesystem::path> recentProjectToRecord(
  const ProjectLocation& location, const bool savedToFolderSinceOpen)
{
  if (location.projectFile.empty() || assetWritesBlocked(isTemplateProject(location), savedToFolderSinceOpen))
  {
    return std::nullopt;
  }

  return location.projectFile;
}

#endif //RECENTPROJECTRECORDING_H
