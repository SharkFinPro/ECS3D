#include "ProjectFingerprint.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

uint64_t fingerprintBytes(const std::string_view bytes)
{
  // FNV-1a, 64 bit: it only has to tell two project files apart, not resist an attacker.
  uint64_t hash = 14695981039346656037ULL;
  for (const char byte : bytes)
  {
    hash ^= static_cast<unsigned char>(byte);
    hash *= 1099511628211ULL;
  }

  return hash == 0 ? 1 : hash;
}

uint64_t fingerprintProjectFile(const std::filesystem::path& projectFile)
{
  if (projectFile.empty())
  {
    return 0;
  }

  std::ifstream file(projectFile, std::ios::binary);
  if (!file.is_open())
  {
    return 0;
  }

  const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  return fingerprintBytes(bytes);
}

namespace
{
  // Fixed so this file needs no terrain headers; terrain/TerrainFormat.h's indexFileName and terrainFolder agree.
  constexpr const char* terrainFolderName = "terrain";
  constexpr const char* terrainIndexName = "terrain.json";

  std::string readBytes(const std::filesystem::path& path)
  {
    std::ifstream file(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  }
}

uint64_t fingerprintProject(const std::filesystem::path& projectFile, const std::filesystem::path& root)
{
  if (projectFile.empty())
  {
    return 0;
  }

  std::ifstream file(projectFile, std::ios::binary);
  if (!file.is_open())
  {
    return 0;
  }

  std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

  std::vector<std::filesystem::path> folders;
  std::error_code error;
  if (!root.empty())
  {
    for (std::filesystem::directory_iterator it(root / terrainFolderName, error), end; !error && it != end;
         it.increment(error))
    {
      if (it->is_directory(error) && std::filesystem::is_regular_file(it->path() / terrainIndexName, error))
      {
        folders.push_back(it->path());
      }
    }
  }

  if (folders.empty())
  {
    return fingerprintBytes(bytes);
  }

  std::ranges::sort(folders);
  for (const auto& folder : folders)
  {
    bytes += folder.filename().generic_string();
    bytes += readBytes(folder / terrainIndexName);
  }

  return fingerprintBytes(bytes);
}