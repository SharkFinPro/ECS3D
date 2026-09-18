#include "UserDataDirectory.h"
#include <cstdlib>

namespace {
  // Absent or empty is the interesting case: getenv returns null on Windows for an unset variable and
  // an empty string is just as useless as one.
  const char* environmentPath(const char* name)
  {
    const char* value = std::getenv(name);

    return value && *value ? value : nullptr;
  }
}

std::filesystem::path userDataDirectory()
{
  std::filesystem::path directory;

#ifdef _WIN32
  if (const char* appData = environmentPath("APPDATA"))
  {
    directory = std::filesystem::path(appData) / "ECS3D";
  }
#elif defined(__APPLE__)
  if (const char* home = environmentPath("HOME"))
  {
    directory = std::filesystem::path(home) / "Library" / "Application Support" / "ECS3D";
  }
#else
  if (const char* configHome = environmentPath("XDG_CONFIG_HOME"))
  {
    directory = std::filesystem::path(configHome) / "ECS3D";
  }
  else if (const char* home = environmentPath("HOME"))
  {
    directory = std::filesystem::path(home) / ".config" / "ECS3D";
  }
#endif

  // With no home directory to resolve against, a path under the working directory still lets an app
  // start and persist.
  if (directory.empty())
  {
    directory = std::filesystem::path("ECS3D");
  }

  return directory;
}

std::filesystem::path defaultLogFile(const std::string_view appName)
{
  return userDataDirectory() / "logs" / (std::string(appName) + ".log");
}
