#ifndef SCRIPTDIRECTORIES_H
#define SCRIPTDIRECTORIES_H

#include <filesystem>
#include <string>
#include <vector>

namespace scripting {
  inline constexpr const char* projectScriptsFolder = "scripts";

  // Folders an IDE or build leaves under a scripts folder; their generated sources must not compile.
  [[nodiscard]] inline bool isIgnoredScriptFolder(const std::string& name)
  {
    return name == "bin" || name == "obj" || (!name.empty() && name.front() == '.');
  }

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

  // The form the bridge's init takes, as UTF-8. No realistic path holds a newline; one that does is skipped.
  [[nodiscard]] inline std::string joinScriptDirectories(const std::vector<std::filesystem::path>& directories)
  {
    std::string joined;
    for (const auto& directory : directories)
    {
      if (!joined.empty())
      {
        joined += '\n';
      }
      const auto utf8 = directory.u8string();
      joined.append(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    }

    return joined;
  }
}

#endif //SCRIPTDIRECTORIES_H
