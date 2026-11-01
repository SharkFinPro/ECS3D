#include "ProjectFingerprint.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <random>
#include <system_error>
#include <string>

namespace {
  void writeFile(const std::filesystem::path& path, const std::string& content)
  {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << content;
  }

  class ProjectFingerprintFiles : public ::testing::Test {
  protected:
    void SetUp() override
    {
      std::random_device device;
      m_directory = std::filesystem::temp_directory_path() / ("ecs3d-fingerprint-" + std::to_string(device()));
      std::filesystem::create_directories(m_directory);
    }

    void TearDown() override
    {
      std::error_code error;
      std::filesystem::remove_all(m_directory, error);
    }

    [[nodiscard]] std::filesystem::path scratchFile(const std::string& name) const
    {
      return m_directory / name;
    }

    std::filesystem::path m_directory;
  };
}

TEST(ProjectFingerprint, MatchesPublishedFnv1a64Values)
{
  EXPECT_EQ(fingerprintBytes("a"), 0xaf63dc4c8601ec8cULL);
  EXPECT_EQ(fingerprintBytes("foobar"), 0x85944171f73967e8ULL);
}

// The empty input is the FNV offset basis. The zero-to-one mapping cannot be tested directly: it needs input
// whose FNV-1a hash is exactly zero, which is impractical to construct.
TEST(ProjectFingerprint, TheEmptyInputIsTheOffsetBasis)
{
  EXPECT_EQ(fingerprintBytes(""), 0xcbf29ce484222325ULL);
  EXPECT_EQ(fingerprintBytes(""), 14695981039346656037ULL);
}

TEST(ProjectFingerprint, EqualBytesGiveEqualFingerprintsAndDifferentBytesDiffer)
{
  EXPECT_EQ(fingerprintBytes("{\"a\":1}"), fingerprintBytes("{\"a\":1}"));
  EXPECT_NE(fingerprintBytes("{\"a\":1}"), fingerprintBytes("{\"a\":2}"));
}

TEST(ProjectFingerprint, EmptyAndShortInputsHashToANonzeroValue)
{
  EXPECT_NE(fingerprintBytes(""), 0u);
  EXPECT_NE(fingerprintBytes("project"), 0u);
}

TEST_F(ProjectFingerprintFiles, AFileMatchesTheFingerprintOfItsBytes)
{
  const auto path = scratchFile("same.ecs3d");
  writeFile(path, "{\"scenes\":[]}");

  EXPECT_EQ(fingerprintProjectFile(path), fingerprintBytes("{\"scenes\":[]}"));
}

TEST_F(ProjectFingerprintFiles, AFileIsReadByteForByteIncludingLineEndings)
{
  const std::string content = "{\"a\":1,\r\n\"b\":2}\r\n";
  const auto path = scratchFile("crlf.ecs3d");
  writeFile(path, content);

  EXPECT_EQ(fingerprintProjectFile(path), fingerprintBytes(content));
  EXPECT_NE(fingerprintProjectFile(path), fingerprintBytes("{\"a\":1,\n\"b\":2}\n"));
}

TEST_F(ProjectFingerprintFiles, TwoFilesWithDifferentContentDiffer)
{
  const auto first = scratchFile("first.ecs3d");
  const auto second = scratchFile("second.ecs3d");
  writeFile(first, "{\"name\":\"one\"}");
  writeFile(second, "{\"name\":\"two\"}");

  EXPECT_NE(fingerprintProjectFile(first), fingerprintProjectFile(second));
}

TEST_F(ProjectFingerprintFiles, AMissingFileOrEmptyPathIsZero)
{
  const auto present = scratchFile("present.ecs3d");
  writeFile(present, "{}");
  ASSERT_NE(fingerprintProjectFile(present), 0u);

  EXPECT_EQ(fingerprintProjectFile(scratchFile("absent.ecs3d")), 0u);
  EXPECT_EQ(fingerprintProjectFile({}), 0u);
}
