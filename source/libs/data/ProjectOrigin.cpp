#include "ProjectOrigin.h"
#include "templates/TemplateManifest.h"
#include <cctype>
#include <cstdlib>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace
{
  constexpr std::size_t maxFolderNameBytes = 64;

#ifdef _WIN32
  std::filesystem::path knownDocumentsFolder()
  {
    // FOLDERID_Documents spelled out, so linking needs only shell32 and ole32 rather than the GUID library.
    const GUID documentsId = { 0xFDD39AD0, 0x238F, 0x46AF, { 0xAD, 0xB4, 0x6C, 0x85, 0x48, 0x03, 0x69, 0xC7 } };

    PWSTR raw = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(documentsId, 0, nullptr, &raw)) && raw)
    {
      result = std::filesystem::path(static_cast<const wchar_t*>(raw));
    }

    CoTaskMemFree(raw);
    return result;
  }

  std::filesystem::path homeDirectory()
  {
    const wchar_t* value = _wgetenv(L"USERPROFILE");
    return value && *value ? std::filesystem::path(value) : std::filesystem::path();
  }
#else
  std::filesystem::path homeDirectory()
  {
    const char* value = std::getenv("HOME");
    return value && *value ? pathFromUtf8(value) : std::filesystem::path();
  }
#endif

  bool isReservedDeviceName(const std::string& name)
  {
    std::string stem = name.substr(0, name.find('.'));
    const auto last = stem.find_last_not_of(' ');
    stem.erase(last == std::string::npos ? 0 : last + 1);
    for (char& c : stem)
    {
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }

    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL")
    {
      return true;
    }

    return stem.size() == 4 && (stem.compare(0, 3, "COM") == 0 || stem.compare(0, 3, "LPT") == 0) &&
           stem[3] >= '1' && stem[3] <= '9';
  }

  std::string trimmedName(const std::string& name)
  {
    const auto first = name.find_first_not_of(' ');
    const auto last = name.find_last_not_of(" .");
    if (first == std::string::npos || last == std::string::npos || last < first)
    {
      return {};
    }

    return name.substr(first, last - first + 1);
  }

  bool isFolderNameCharacter(const char c)
  {
    constexpr std::string_view forbidden = "<>:\"/\\|?*";
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

    if (name.size() > maxFolderNameBytes)
    {
      auto cut = maxFolderNameBytes;
      while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80)
      {
        --cut;
      }
      name.resize(cut);
    }

    name = trimmedName(name);
    if (name.empty())
    {
      return "New Project";
    }

    return isReservedDeviceName(name) ? name + " Project" : name;
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
  std::filesystem::path documents;
#ifdef _WIN32
  documents = knownDocumentsFolder();
#endif

  if (documents.empty())
  {
    const auto home = homeDirectory();
    if (home.empty())
    {
      return {};
    }

    documents = home / "Documents";
  }

  return documents / "ECS3D Projects";
}

bool folderIsFreeForNewProject(const std::filesystem::path& folder)
{
  std::error_code error;
  if (!std::filesystem::exists(folder, error))
  {
    return !error;
  }

  if (!std::filesystem::is_directory(folder, error) || error)
  {
    return false;
  }

  // A failed earlier save leaves empty assets/ and scripts/ behind; that must not block a retry.
  for (const auto& entry : std::filesystem::directory_iterator(folder, error))
  {
    const auto name = entry.path().filename();
    const bool isProjectSubfolder = name == "assets" || name == "scripts";
    if (!isProjectSubfolder || !entry.is_directory(error) || !std::filesystem::is_empty(entry.path(), error))
    {
      return false;
    }
  }

  return !error;
}

std::filesystem::path defaultSaveFolder(const std::filesystem::path& projectsDir, const std::string& templateName,
                                        const std::function<bool(const std::filesystem::path&)>& isTaken)
{
  const auto name = folderNameFrom(templateName);

  auto candidate = projectsDir / pathFromUtf8(name.c_str());
  for (int suffix = 2; isTaken(candidate) && suffix < 100000; ++suffix)
  {
    candidate = projectsDir / pathFromUtf8((name + " " + std::to_string(suffix)).c_str());
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
