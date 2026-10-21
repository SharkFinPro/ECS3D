#include "ProjectFingerprint.h"
#include <fstream>
#include <iterator>
#include <string>

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
