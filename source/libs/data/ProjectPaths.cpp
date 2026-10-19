#include "ProjectPaths.h"
#include <system_error>

std::string quotedProjectArgument(const std::string& project)
{
  auto end = project.find_last_not_of("/\\");
  std::string value = end == std::string::npos ? project.substr(0, 1) : project.substr(0, end + 1);

  const bool isRoot = end == std::string::npos || value.back() == ':';
  if (isRoot)
  {
    value += project.size() > value.size() ? project.substr(value.size(), 1) : std::string();
    value += ".";
  }

  return "\"" + value + "\"";
}

ProjectLocation locateProject(const std::filesystem::path& argument)
{
  if (argument.empty())
  {
    return {};
  }

  std::error_code error;
  if (std::filesystem::is_directory(argument, error))
  {
    return {argument / projectFileName, argument};
  }

  if (argument.extension() == projectFileExtension)
  {
    return {argument, argument.parent_path()};
  }

  return {argument, {}};
}

std::optional<std::filesystem::path> resolveAssetPath(const std::filesystem::path& projectRoot,
                                                      const std::string& filePath)
{
  const std::filesystem::path path(filePath);

  if (path.is_absolute() || path.empty())
  {
    return path;
  }

  if (path.has_root_path())
  {
    return std::nullopt;
  }

  const auto normal = path.lexically_normal();
  if (!normal.empty() && *normal.begin() == "..")
  {
    return std::nullopt;
  }

  if (!projectRoot.empty())
  {
    const auto candidate = projectRoot / path;
    std::error_code error;
    if (std::filesystem::exists(candidate, error))
    {
      return candidate;
    }
  }

  return path;
}
