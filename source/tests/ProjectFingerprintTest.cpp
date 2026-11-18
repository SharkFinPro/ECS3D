#include "ProjectFingerprint.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainFiles.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainVolume.h"
#include <array>
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

TEST_F(ProjectFingerprintFiles, AProjectWithoutTerrainKeepsTheFingerprintOfItsFile)
{
  const auto path = scratchFile("plain.ecs3d");
  writeFile(path, "{\"scenes\":[]}");

  EXPECT_EQ(fingerprintProject(path, m_directory), fingerprintProjectFile(path));
  EXPECT_EQ(fingerprintProject(path, {}), fingerprintProjectFile(path));

  std::filesystem::create_directories(m_directory / "terrain" / "empty-folder");
  EXPECT_EQ(fingerprintProject(path, m_directory), fingerprintProjectFile(path));
}

TEST_F(ProjectFingerprintFiles, ATerrainIndexChangesTheProjectFingerprint)
{
  const auto path = scratchFile("terrain.ecs3d");
  writeFile(path, "{}");
  const auto plain = fingerprintProject(path, m_directory);

  const auto folder = m_directory / "terrain" / "11111111-1111-1111-1111-111111111111";
  std::filesystem::create_directories(folder);
  writeFile(folder / "terrain.json", "{\"regions\":[1]}");
  const auto first = fingerprintProject(path, m_directory);
  EXPECT_NE(first, plain);
  EXPECT_EQ(fingerprintProject(path, m_directory), first);

  writeFile(folder / "terrain.json", "{\"regions\":[2]}");
  EXPECT_NE(fingerprintProject(path, m_directory), first);
}

TEST_F(ProjectFingerprintFiles, TerrainFolderCreationOrderDoesNotMatter)
{
  const auto path = scratchFile("order.ecs3d");
  writeFile(path, "{}");

  const auto makeRoot = [&](const std::string& name, const bool aFirst)
  {
    const auto root = m_directory / name;
    std::filesystem::create_directories(root);
    const std::array<const char*, 2> ascendingOrder{ "aaaa", "bbbb" };
    const std::array<const char*, 2> descendingOrder{ "bbbb", "aaaa" };
    for (const char* folder : aFirst ? ascendingOrder : descendingOrder)
    {
      std::filesystem::create_directories(root / "terrain" / folder);
      writeFile(root / "terrain" / folder / "terrain.json", std::string("{\"id\":\"") + folder + "\"}");
    }
    return root;
  };

  const auto ascending = makeRoot("ascending", true);
  const auto descending = makeRoot("descending", false);
  EXPECT_EQ(fingerprintProject(path, ascending), fingerprintProject(path, descending));
  EXPECT_NE(fingerprintProject(path, ascending), fingerprintProjectFile(path));
}

TEST_F(ProjectFingerprintFiles, AnEmptyOrMissingProjectFileIsZeroEvenWithTerrain)
{
  const auto folder = m_directory / "terrain" / "aaaa";
  std::filesystem::create_directories(folder);
  writeFile(folder / "terrain.json", "{}");

  EXPECT_EQ(fingerprintProject({}, m_directory), 0u);
  EXPECT_EQ(fingerprintProject(scratchFile("absent.ecs3d"), m_directory), 0u);
}

TEST_F(ProjectFingerprintFiles, ATerrainSavedWithSaveTerrainChangesTheFingerprint)
{
  const auto path = scratchFile("saved.ecs3d");
  writeFile(path, "{}");
  const auto uuid = uuids::uuid::from_string("11111111-1111-1111-1111-111111111111").value();

  terrain::TerrainVolume volume(2.5f);
  volume.setChunk({ 0, 0, 0 }, terrain::TerrainChunk::uniform(-terrain::airDistance, 1));
  ASSERT_TRUE(terrain::saveTerrain(m_directory, { .uuid = uuid }, volume).ok);
  const auto first = fingerprintProject(path, m_directory);
  EXPECT_NE(first, fingerprintProjectFile(path));

  volume.setChunk({ 1, 0, 0 }, terrain::TerrainChunk::uniform(-terrain::airDistance, 2));
  ASSERT_TRUE(terrain::saveTerrain(m_directory, { .uuid = uuid }, volume).ok);
  EXPECT_NE(fingerprintProject(path, m_directory), first);
}

TEST_F(ProjectFingerprintFiles, RenamingATerrainFolderChangesTheFingerprint)
{
  const auto path = scratchFile("rename.ecs3d");
  writeFile(path, "{}");
  const auto folder = m_directory / "terrain" / "aaaa";
  std::filesystem::create_directories(folder);
  writeFile(folder / "terrain.json", "{}");
  const auto before = fingerprintProject(path, m_directory);
  ASSERT_NE(before, fingerprintProjectFile(path));

  std::filesystem::rename(folder, m_directory / "terrain" / "bbbb");
  EXPECT_NE(fingerprintProject(path, m_directory), before);
}

TEST_F(ProjectFingerprintFiles, AnUnreadableTerrainIndexMakesTheProjectUnfingerprintable)
{
  const auto path = scratchFile("unreadable.ecs3d");
  writeFile(path, "{}");
  ASSERT_NE(fingerprintProject(path, m_directory), 0u);

  std::filesystem::create_directories(m_directory / "terrain" / "aaaa" / "terrain.json");
  EXPECT_EQ(fingerprintProject(path, m_directory), 0u);
  EXPECT_NE(fingerprintProjectFile(path), 0u);
}
