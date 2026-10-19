#include "ProjectPaths.h"
#include <system_error>

namespace
{
  bool isAsciiLetter(const char c)
  {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
  }

  bool hasProjectExtension(const std::filesystem::path& path)
  {
    std::string extension = path.extension().string();
    for (char& c : extension)
    {
      if (c >= 'A' && c <= 'Z')
      {
        c = static_cast<char>(c - 'A' + 'a');
      }
    }

    return extension == projectFileExtension;
  }
}

std::string quotedProjectArgument(const std::string& project)
{
  const auto end = project.find_last_not_of("/\\");
  std::string value = end == std::string::npos ? project.substr(0, 1) : project.substr(0, end + 1);

  const bool isDrive = value.size() == 2 && isAsciiLetter(value[0]) && value[1] == ':';
  if (end == std::string::npos || isDrive)
  {
    if (project.size() > value.size())
    {
      value += project[value.size()];
    }
    value += ".";
  }

  return "\"" + value + "\"";
}

std::string absoluteProjectArgument(const std::string& project)
{
  if (project.empty())
  {
    return project;
  }

  std::error_code error;
  const auto absolute = std::filesystem::absolute(project, error);

  return error ? project : absolute.string();
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

  if (hasProjectExtension(argument))
  {
    auto parent = argument.parent_path();
    return {argument, parent.empty() ? std::filesystem::path(".") : parent};
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

std::filesystem::path projectFileInFolder(const std::filesystem::path& folder)
{
  return folder / projectFileName;
}

bool isLegacyProjectFile(const std::filesystem::path& file)
{
  return !file.empty() && !hasProjectExtension(file);
}

bool ensureProjectFolders(const std::filesystem::path& root, std::error_code& error)
{
  for (const auto& folder : { root, root / "assets", root / "scripts" })
  {
    std::filesystem::create_directories(folder, error);
    if (error)
    {
      return false;
    }
  }

  return true;
}

void copyProjectContent(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& error)
{
  error.clear();
  if (from.empty() || to.empty())
  {
    return;
  }

  const auto source = std::filesystem::weakly_canonical(from, error);
  if (error)
  {
    return;
  }

  const auto target = std::filesystem::weakly_canonical(to, error);
  if (error)
  {
    return;
  }

  const auto relation = target.lexically_relative(source);
  if (!relation.empty() && *relation.begin() != "..")
  {
    return;
  }

  for (const char* sub : { "assets", "scripts" })
  {
    if (!std::filesystem::is_directory(source / sub, error))
    {
      error.clear();
      continue;
    }

    std::filesystem::copy(source / sub, target / sub,
                          std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing,
                          error);
    if (error)
    {
      return;
    }
  }
}

ImportDestination importDestination(const std::filesystem::path& root, const std::filesystem::path& sourceFile,
                                    const std::string& name, const ImportKind kind)
{
  const std::filesystem::path folder =
    std::filesystem::path("assets") / (kind == ImportKind::model ? "models" : "textures");
  const std::string stem = name.empty() ? sourceFile.stem().string() : name;
  const std::string extension = sourceFile.extension().string();

  std::string fileName = stem + extension;
  if (!root.empty())
  {
    std::error_code error;
    for (int suffix = 1; std::filesystem::exists(root / folder / fileName, error) && suffix < 100000; ++suffix)
    {
      fileName = stem + "-" + std::to_string(suffix) + extension;
    }
  }

  return { root / folder / fileName, (folder / fileName).generic_string() };
}
