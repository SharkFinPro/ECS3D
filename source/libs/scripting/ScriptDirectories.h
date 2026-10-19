#ifndef SCRIPTDIRECTORIES_H
#define SCRIPTDIRECTORIES_H

#include <filesystem>
#include <string>
#include <vector>

namespace scripting {
  inline constexpr const char* projectScriptsFolder = "scripts";

  // The directories the bridge compiles together: the built-in scripts first, then the project's own when a
  // project folder is in use. A project folder without scripts/ is normal; the bridge skips a missing one.
  [[nodiscard]] inline std::vector<std::filesystem::path> scriptDirectories(const std::filesystem::path& builtIn,
                                                                            const std::filesystem::path& projectRoot)
  {
    std::vector<std::filesystem::path> directories{ builtIn };
    if (!projectRoot.empty())
    {
      directories.push_back(projectRoot / projectScriptsFolder);
    }

    return directories;
  }

  // The form the bridge's init takes. A newline cannot appear in a path on any supported platform.
  [[nodiscard]] inline std::string joinScriptDirectories(const std::vector<std::filesystem::path>& directories)
  {
    std::string joined;
    for (const auto& directory : directories)
    {
      if (!joined.empty())
      {
        joined += '\n';
      }
      joined += directory.string();
    }

    return joined;
  }
}

#endif //SCRIPTDIRECTORIES_H
