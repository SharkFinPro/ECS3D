#include "ProjectFingerprint.h"
#include "ProjectPaths.h"
#include "terrain/TerrainFiles.h"
#include "terrain/TerrainFormat.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>
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
  bool readBytes(const std::filesystem::path& path, std::string& bytes)
  {
    std::error_code error;
    std::ifstream file(path, std::ios::binary);
    if (!std::filesystem::is_regular_file(path, error) || !file.is_open())
    {
      return false;
    }

    bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
  }
}

uint64_t fingerprintProject(const std::filesystem::path& projectFile, const std::filesystem::path& root)
{
  std::string bytes;
  if (projectFile.empty() || !readBytes(projectFile, bytes))
  {
    return 0;
  }

  if (root.empty())
  {
    return fingerprintBytes(bytes);
  }

  // The terrain folder is the parent of any terrain's own folder.
  const auto terrainRoot = terrain::terrainFolder(root, {}).parent_path();

  std::error_code error;
  if (!std::filesystem::is_directory(terrainRoot, error))
  {
    return fingerprintBytes(bytes);
  }

  std::vector<std::pair<std::string, std::filesystem::path>> indexes;
  for (std::filesystem::directory_iterator it(terrainRoot, error), end; !error && it != end; it.increment(error))
  {
    const auto index = it->path() / terrain::indexFileName;
    std::error_code entryError;
    if (!it->is_directory(entryError) || !std::filesystem::exists(index, entryError))
    {
      continue;
    }

    indexes.emplace_back(pathToUtf8(it->path().filename()), index);
  }

  // A half-read terrain would fingerprint as a different project; zero makes the identity check skip instead.
  if (error)
  {
    return 0;
  }

  std::ranges::sort(indexes);
  for (const auto& [name, index] : indexes)
  {
    std::string indexBytes;
    if (!readBytes(index, indexBytes))
    {
      return 0;
    }

    bytes += name;
    bytes += indexBytes;
  }

  return fingerprintBytes(bytes);
}
