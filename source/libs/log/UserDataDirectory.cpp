#include "UserDataDirectory.h"
#include <cstdlib>
#include <functional>

namespace {
  // Absent or empty is the interesting case: getenv returns null on Windows for an unset variable and
  // an empty string is just as useless as one.
  const char* environmentPath(const std::function<const char*(const char*)>& lookup, const char* name)
  {
    const char* value = lookup(name);

    return value && *value ? value : nullptr;
  }
}

std::filesystem::path userDataDirectory()
{
  return userDataDirectory([](const char* name) { return std::getenv(name); });
}

std::filesystem::path userDataDirectory(const std::function<const char*(const char*)>& lookup)
{
  std::filesystem::path directory;

#ifdef _WIN32
  if (const char* appData = environmentPath(lookup, "APPDATA"))
  {
    directory = std::filesystem::path(appData) / "ECS3D";
  }
#elif defined(__APPLE__)
  if (const char* home = environmentPath(lookup, "HOME"))
  {
    directory = std::filesystem::path(home) / "Library" / "Application Support" / "ECS3D";
  }
#else
  if (const char* configHome = environmentPath(lookup, "XDG_CONFIG_HOME"))
  {
    directory = std::filesystem::path(configHome) / "ECS3D";
  }
  else if (const char* home = environmentPath(lookup, "HOME"))
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
