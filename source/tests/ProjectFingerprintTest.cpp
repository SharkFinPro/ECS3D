#include "ProjectFingerprint.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
  std::filesystem::path scratchFile(const std::string& name)
  {
    const auto folder = std::filesystem::temp_directory_path() / "ecs3d-fingerprint-test";
    std::filesystem::create_directories(folder);
    return folder / name;
  }

  void writeFile(const std::filesystem::path& path, const std::string& content)
  {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << content;
  }
}

TEST(ProjectFingerprint, EqualBytesGiveEqualFingerprintsAndDifferentBytesDiffer)
{
  EXPECT_EQ(fingerprintBytes("{\"a\":1}"), fingerprintBytes("{\"a\":1}"));
  EXPECT_NE(fingerprintBytes("{\"a\":1}"), fingerprintBytes("{\"a\":2}"));
}

TEST(ProjectFingerprint, ARealFileNeverHashesToZero)
{
  EXPECT_NE(fingerprintBytes(""), 0u);
  EXPECT_NE(fingerprintBytes("project"), 0u);
}

TEST(ProjectFingerprint, AFileMatchesTheFingerprintOfItsBytes)
{
  const auto path = scratchFile("same.ecs3d");
  writeFile(path, "{\"scenes\":[]}");

  EXPECT_EQ(fingerprintProjectFile(path), fingerprintBytes("{\"scenes\":[]}"));
}

TEST(ProjectFingerprint, TwoFilesWithDifferentContentDiffer)
{
  const auto first = scratchFile("first.ecs3d");
  const auto second = scratchFile("second.ecs3d");
  writeFile(first, "{\"name\":\"one\"}");
  writeFile(second, "{\"name\":\"two\"}");

  EXPECT_NE(fingerprintProjectFile(first), fingerprintProjectFile(second));
}

TEST(ProjectFingerprint, AMissingFileOrEmptyPathIsZero)
{
  const auto present = scratchFile("present.ecs3d");
  writeFile(present, "{}");
  ASSERT_NE(fingerprintProjectFile(present), 0u);

  EXPECT_EQ(fingerprintProjectFile(scratchFile("absent.ecs3d")), 0u);
  EXPECT_EQ(fingerprintProjectFile({}), 0u);
}
