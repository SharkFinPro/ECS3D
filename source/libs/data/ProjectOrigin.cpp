#include "ProjectOrigin.h"
#include "templates/TemplateManifest.h"
#include <cstdlib>
#include <string_view>
#include <system_error>

namespace
{
  const char* environmentValue(const char* name)
  {
    const char* value = std::getenv(name);
    return value && *value ? value : nullptr;
  }

  bool isFolderNameCharacter(const char c)
  {
    constexpr std::string_view forbidden = "<>:\"/\|?*";
    return static_cast<unsigned char>(c) >= 0x20 && forbidden.find(c) == std::string_view::npos;
  }

  std::string folderNameFrom(const std::string& text)
  {
    std::string name;
    for (const char c : text)
    {
      if (isFolderNameCharacter(c))
      {
        name += c;
      }
    }

    const auto first = name.find_first_not_of(' ');
    const auto last = name.find_last_not_of(" .");
    if (first == std::string::npos || last == std::string::npos || last < first)
    {
      return "New Project";
    }

    return name.substr(first, last - first + 1);
  }
}

bool isTemplateProject(const ProjectLocation& location)
{
  if (location.root.empty())
  {
    return location.projectFile.empty();
  }

  std::error_code error;
  return std::filesystem::exists(location.root / templateManifestFileName, error);
}

std::optional<std::string> templateDisplayName(const ProjectLocation& location)
{
  if (location.root.empty())
  {
    return std::nullopt;
  }

  if (const auto manifest = loadTemplateManifest(location.root))
  {
    return manifest->name;
  }

  const auto folderName = pathToUtf8(location.root.lexically_normal().filename());
  if (folderName.empty() || folderName == ".")
  {
    return std::nullopt;
  }

  return folderName;
}

std::string untitledTitle(const std::optional<std::string>& templateName)
{
  return "Untitled (from " + (templateName ? *templateName : std::string("the built-in project")) + ")";
}

std::filesystem::path defaultProjectsDirectory()
{
#ifdef _WIN32
  const char* home = environmentValue("USERPROFILE");
#else
  const char* home = environmentValue("HOME");
#endif

  if (!home)
  {
    return {};
  }

  return pathFromUtf8(home) / "Documents" / "ECS3D Projects";
}

bool folderIsFreeForNewProject(const std::filesystem::path& folder)
{
  std::error_code error;
  if (!std::filesystem::exists(folder, error))
  {
    return !error;
  }

  return std::filesystem::is_directory(folder, error) && std::filesystem::is_empty(folder, error) && !error;
}

std::filesystem::path defaultSaveFolder(const std::filesystem::path& projectsDir, const std::string& templateName,
                                        const std::function<bool(const std::filesystem::path&)>& isTaken)
{
  const auto name = folderNameFrom(templateName);

  auto candidate = projectsDir / name;
  for (int suffix = 2; isTaken(candidate) && suffix < 100000; ++suffix)
  {
    candidate = projectsDir / (name + " " + std::to_string(suffix));
  }

  return candidate;
}

std::filesystem::path defaultSaveFolder(const std::filesystem::path& projectsDir, const std::string& templateName)
{
  return defaultSaveFolder(projectsDir, templateName,
                           [](const std::filesystem::path& folder) { return !folderIsFreeForNewProject(folder); });
}

std::optional<std::string> templateSaveRefusal(const std::filesystem::path& folder,
                                               const std::filesystem::path& templateRoot)
{
  if (!folderIsFreeForNewProject(folder))
  {
    return "the folder is not empty";
  }

  if (!templateRoot.empty())
  {
    std::error_code error;
    const auto target = std::filesystem::weakly_canonical(folder, error);
    const auto source = error ? std::filesystem::path() : std::filesystem::weakly_canonical(templateRoot, error);
    if (error)
    {
      return "the folder could not be compared with the template's folder";
    }

    const auto relation = target.lexically_relative(source);
    if (!relation.empty() && *relation.begin() != "..")
    {
      return "the folder is the template's own folder or lies inside it";
    }
  }

  return std::nullopt;
}
