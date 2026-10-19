#ifndef PROJECTPATHS_H
#define PROJECTPATHS_H

#include <filesystem>
#include <optional>
#include <string>

inline constexpr const char* projectFileName = "project.ecs3d";

inline constexpr const char* projectFileExtension = ".ecs3d";

struct ProjectLocation {
  std::filesystem::path projectFile;
  // Empty means no project folder: only the built-in assets next to the executable are available.
  std::filesystem::path root;
};

// Maps a --project argument to the file to load and the folder that owns the project's assets.
// A directory is a project folder holding projectFileName; a ".ecs3d" file's root is its parent directory;
// any other file (a legacy bare .json project) has no root. An empty argument yields an empty location.
// A path that does not exist is treated as a file, so the load reports it the way it always has.
[[nodiscard]] ProjectLocation locateProject(const std::filesystem::path& argument);

// Resolves an asset path recorded in a project. An absolute path is returned as is. A relative path that
// climbs out of its base (leading "..") or carries a root name or directory is refused. Otherwise the
// project's copy wins when projectRoot is set and the file exists there; failing that, filePath is returned
// as given, relative to the working directory (the executable's directory, where the built-in assets live).
[[nodiscard]] std::optional<std::filesystem::path> resolveAssetPath(const std::filesystem::path& projectRoot,
                                                                    const std::string& filePath);

// A --project value ready to put on a spawned server's command line: double quoted, with trailing separators
// removed since a final backslash would escape the closing quote. A root such as "C:\\" keeps a separator
// and gains a "." ("C:\."), because "C:" alone means the drive's current directory.
[[nodiscard]] std::string quotedProjectArgument(const std::string& project);

#endif //PROJECTPATHS_H
