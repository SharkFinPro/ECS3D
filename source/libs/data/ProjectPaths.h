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
// any other file (a legacy bare .json project) has no root. The extension compares
// ASCII case-insensitively. An empty argument yields an empty location.
// A path that does not exist is treated as a file, so the load reports it the way it always has.
[[nodiscard]] ProjectLocation locateProject(const std::filesystem::path& argument);

// Resolves an asset path recorded in a project. An absolute path is returned as is. A relative path that
// climbs out of its base (leading "..", refused even with no project root: assets must stay inside the project
// or the built-in tree) or carries a root name or directory is refused.
// Absolute paths pass through on purpose. Otherwise the project's copy wins when projectRoot is set and the
// file exists there; failing that, filePath is returned as given, relative to the working directory (the executable's directory, where the built-in assets live).
[[nodiscard]] std::optional<std::filesystem::path> resolveAssetPath(const std::filesystem::path& projectRoot,
                                                                    const std::string& filePath);

// A --project value ready to put on a spawned server's command line: double quoted, with trailing separators
// removed since a final backslash would escape the closing quote. A drive root such as "C:\" keeps a
// separator and gains a "." ("C:\."), because "C:" alone means the drive's current directory. A value
// holding a double quote is not supported.
[[nodiscard]] std::string quotedProjectArgument(const std::string& project);

// Makes a --project value absolute against the current directory, so an app and the server it spawns (which
// runs from the executable's directory) agree on it. Empty stays empty; on failure the value is unchanged.
[[nodiscard]] std::string absoluteProjectArgument(const std::string& project);

#endif //PROJECTPATHS_H
