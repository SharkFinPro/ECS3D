#ifndef PROJECTPATHS_H
#define PROJECTPATHS_H

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

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

// The project file a project folder holds.
[[nodiscard]] std::filesystem::path projectFileInFolder(const std::filesystem::path& folder);

// True for a project file path that is not a ".ecs3d" one (a bare .json from before project folders). Saving it writes the same file; only Save As moves it into a folder. An empty path is not legacy.
[[nodiscard]] bool isLegacyProjectFile(const std::filesystem::path& file);

// Creates the folder and its assets/ and scripts/ subfolders. False (error filled in) when one cannot be made.
[[nodiscard]] bool ensureProjectFolders(const std::filesystem::path& root, std::error_code& error);

// Copies the project's own assets/ and scripts/ from one folder into another, keeping files the target
// already has. Does nothing for an empty source, the same folder, or a target inside the source.
void copyProjectContent(const std::filesystem::path& from, const std::filesystem::path& to,
                        std::error_code& error);

enum class ImportKind {
  model,
  texture
};

struct ImportDestination {
  std::filesystem::path absolute;
  // Forward slashes, relative to the project root: what the asset record stores.
  std::string relative;
};

// Where an imported file goes inside a project: <root>/assets/models or <root>/assets/textures, named
// <name><source extension> (the source's own stem when name is empty). A name already taken there gains
// "-1", "-2", ... before the extension. With an empty root the destination is relative to the working
// directory and an existing file is overwritten, as before project folders.
[[nodiscard]] ImportDestination importDestination(const std::filesystem::path& root,
                                                  const std::filesystem::path& sourceFile,
                                                  const std::string& name, ImportKind kind);

#endif //PROJECTPATHS_H
