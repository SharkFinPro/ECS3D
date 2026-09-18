#ifndef USERDATADIRECTORY_H
#define USERDATADIRECTORY_H

#include <filesystem>
#include <string>
#include <string_view>

// Where ECS3D keeps per-user, per-machine state: settings and logs, never project data, which is
// authoritative on the server and replicated to every client. The log library owns the resolution
// because it is the lowest layer every app links, including the headless server, which does not link
// ECS3DSettings.

// %APPDATA%/ECS3D on Windows, ~/Library/Application Support/ECS3D on macOS, $XDG_CONFIG_HOME (or
// ~/.config) /ECS3D elsewhere. With no home directory in the environment this is the relative path
// "ECS3D", so an app can still start and persist under its working directory.
[[nodiscard]] std::filesystem::path userDataDirectory();

[[nodiscard]] std::filesystem::path defaultLogFile(std::string_view appName);

#endif  // USERDATADIRECTORY_H
