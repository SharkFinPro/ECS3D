#include <gtest/gtest.h>

#include "ProjectPaths.h"
#include "assets/AssetRegistry.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainFiles.h"
#include "terrain/TerrainFormat.h"
#include "terrain/TerrainReplication.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainStreamer.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"
#include <TerrainSaving.h>
#include <Protocol.h>

#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include "TerrainFilesFixtures.h"

namespace {
  const auto denseUuid = uuids::uuid::from_string("11111111-1111-4111-8111-111111111111").value();
  const auto uniformUuid = uuids::uuid::from_string("22222222-2222-4222-8222-222222222222").value();
  const auto unheldUuid = uuids::uuid::from_string("33333333-3333-4333-8333-333333333333").value();

  AssetRecord terrainRecord(const uuids::uuid& uuid)
  {
    return { .uuid = uuid, .type = AssetType::Terrain, .path = terrainAssetPath(uuid) };
  }

  terrain::TerrainDescription describe(const uuids::uuid& uuid, const float voxelSize)
  {
    terrain::TerrainDescription description;
    description.uuid = uuid;
    description.voxelSize = voxelSize;
    description.palette.push_back({ "grass", uuid, uuids::uuid() });
    description.palette.push_back({ "rock", uuids::uuid(), uuid });
    return description;
  }

  // Two terrains: one of dense chunks across several regions and one of uniform chunks.
  void fillSample(AssetRegistry& registry, terrain::TerrainStore& store)
  {
    registry.registerAsset(terrainRecord(denseUuid));
    registry.registerAsset(terrainRecord(uniformUuid));

    const auto dense = std::make_shared<terrain::TerrainVolume>(2.5f);
    dense->setChunk({ 0, 0, 0 }, terrainFilesFixtures::denseChunk(1));
    dense->setChunk({ -1, -1, -1 }, terrainFilesFixtures::denseChunk(2));
    dense->setChunk({ 9, 0, 0 }, terrainFilesFixtures::denseChunk(3));
    ASSERT_TRUE(store.adopt(describe(denseUuid, 2.5f), dense));

    const auto uniform = std::make_shared<terrain::TerrainVolume>(4.0f);
    uniform->setChunk({ 0, 0, 0 }, terrain::TerrainChunk::uniform(-terrain::airDistance, 1));
    uniform->setChunk({ 1, 0, 0 }, terrain::TerrainChunk::uniform(-terrain::airDistance, 2));
    ASSERT_TRUE(store.adopt(describe(uniformUuid, 4.0f), uniform));
  }

  void expectSameTerrain(const terrain::TerrainStore& expected, const terrain::TerrainStore& actual,
                         const uuids::uuid& uuid)
  {
    const auto want = expected.find(uuid);
    const auto got = actual.find(uuid);
    ASSERT_NE(want, nullptr);
    ASSERT_NE(got, nullptr);
    ASSERT_FALSE(want->chunks().empty());
    ASSERT_EQ(got->chunks().size(), want->chunks().size());

    for (const auto& [coord, chunk] : want->chunks())
    {
      const auto held = got->chunk(coord);
      ASSERT_NE(held, nullptr);
      EXPECT_EQ(held->hash(), chunk->hash());
    }

    const auto* wantDescription = expected.description(uuid);
    const auto* gotDescription = actual.description(uuid);
    ASSERT_NE(wantDescription, nullptr);
    ASSERT_NE(gotDescription, nullptr);
    EXPECT_NEAR(gotDescription->voxelSize, wantDescription->voxelSize, 1.0e-6f);
    ASSERT_EQ(gotDescription->palette.size(), wantDescription->palette.size());

    for (size_t i = 0; i < wantDescription->palette.size(); ++i)
    {
      EXPECT_EQ(gotDescription->palette[i].name, wantDescription->palette[i].name);
      EXPECT_EQ(gotDescription->palette[i].albedoTexture, wantDescription->palette[i].albedoTexture);
      EXPECT_EQ(gotDescription->palette[i].specularTexture, wantDescription->palette[i].specularTexture);
    }
  }

  class TerrainProjectSaveTest : public testing::Test
  {
  protected:
    void SetUp() override
    {
      const auto* info = testing::UnitTest::GetInstance()->current_test_info();
      std::random_device device;
      m_base = std::filesystem::temp_directory_path() /
               (std::string("ecs3d_terrain_save_") + info->name() + "_" + std::to_string(device()));
      std::filesystem::remove_all(m_base);
      std::filesystem::create_directories(m_base);
    }

    void TearDown() override
    {
      std::error_code ignored;
      std::filesystem::remove_all(m_base, ignored);
    }

    [[nodiscard]] std::filesystem::path root(const char* name) const
    {
      const auto folder = m_base / name;
      std::filesystem::create_directories(folder);
      return folder;
    }

    std::filesystem::path m_base;
  };
}

TEST_F(TerrainProjectSaveTest, SavedTerrainsLoadBackWithTheSameChunksAndDescriptions)
{
  AssetRegistry registry;
  terrain::TerrainStore store;
  fillSample(registry, store);
  const auto first = root("first");

  const auto report = terrain::saveProjectTerrains(first, registry, store);
  EXPECT_EQ(report.saved, 2u);
  EXPECT_TRUE(report.errors.empty());

  terrain::TerrainStore loaded;
  loaded.reconcile(registry);

  // Positive control: the fresh store holds both terrains, empty.
  ASSERT_NE(loaded.find(denseUuid), nullptr);
  EXPECT_TRUE(loaded.find(denseUuid)->chunks().empty());

  const auto loadReport = terrain::loadProjectTerrains(first, registry, loaded);
  EXPECT_EQ(loadReport.loaded, 2u);
  EXPECT_TRUE(loadReport.errors.empty());

  expectSameTerrain(store, loaded, denseUuid);
  expectSameTerrain(store, loaded, uniformUuid);
}

TEST_F(TerrainProjectSaveTest, AnEmptyRootWritesNothing)
{
  AssetRegistry registry;
  terrain::TerrainStore store;
  fillSample(registry, store);

  const auto report = terrain::saveProjectTerrains({}, registry, store);
  EXPECT_EQ(report.saved, 0u);
  EXPECT_TRUE(report.errors.empty());

  // Positive control: the same registry and store with a real root save both terrains.
  EXPECT_EQ(terrain::saveProjectTerrains(root("real"), registry, store).saved, 2u);
}

TEST_F(TerrainProjectSaveTest, ARecordTheStoreDoesNotHoldIsSkipped)
{
  AssetRegistry registry;
  terrain::TerrainStore store;
  fillSample(registry, store);
  registry.registerAsset(terrainRecord(unheldUuid));
  const auto target = root("target");

  const auto report = terrain::saveProjectTerrains(target, registry, store);
  EXPECT_EQ(report.saved, 2u);
  EXPECT_TRUE(report.errors.empty());

  EXPECT_FALSE(std::filesystem::exists(terrain::terrainFolder(target, unheldUuid)));
  EXPECT_TRUE(std::filesystem::exists(terrain::terrainFolder(target, denseUuid) / terrain::indexFileName));
  EXPECT_TRUE(std::filesystem::exists(terrain::terrainFolder(target, uniformUuid) / terrain::indexFileName));
}

TEST_F(TerrainProjectSaveTest, ARootThatIsAFileReportsAnErrorPerTerrain)
{
  AssetRegistry registry;
  terrain::TerrainStore store;
  fillSample(registry, store);

  const auto file = m_base / "not-a-folder";
  std::ofstream(file) << "a file";
  ASSERT_TRUE(std::filesystem::is_regular_file(file));

  const auto report = terrain::saveProjectTerrains(file, registry, store);
  EXPECT_EQ(report.saved, 0u);
  ASSERT_EQ(report.errors.size(), 2u);
  EXPECT_NE(report.errors[0].find(pathToUtf8(file)), std::string::npos) << report.errors[0];
  EXPECT_TRUE(std::filesystem::is_regular_file(file));
}

TEST_F(TerrainProjectSaveTest, AViewFilledThroughTheStreamSavesTheServersTerrain)
{
  AssetRegistry registry;
  terrain::TerrainStore original;
  fillSample(registry, original);
  const auto serverRoot = root("server");
  ASSERT_EQ(terrain::saveProjectTerrains(serverRoot, registry, original).saved, 2u);

  terrain::TerrainStore server;
  server.reconcile(registry);
  ASSERT_EQ(terrain::loadProjectTerrains(serverRoot, registry, server).loaded, 2u);
  for (const auto& uuid : server.terrains())
  {
    server.stampAllChunks(uuid);
  }

  terrain::TerrainStore view;
  view.reconcile(registry);
  for (const auto& uuid : server.terrains())
  {
    ASSERT_TRUE(view.applyDescription(*server.description(uuid)));
  }

  terrain::TerrainStreamer streamer;
  streamer.start(1, server, [](const uuids::uuid&, const terrain::TerrainDescription&) { return glm::vec3(0.0f); });

  const auto toView = [&view](const int32_t, const net::Message& message)
  {
    const auto payload = terrain::unpackTerrainChunks(message);
    ASSERT_TRUE(payload.has_value());
    EXPECT_EQ(terrain::applyTerrainChunks(view, *payload).applied, payload->entries.size());
  };

  for (int i = 0; i < 1000 && streamer.streaming(1); ++i)
  {
    streamer.pump(server, toView, terrain::defaultStreamBudgetBytes);
  }

  ASSERT_FALSE(streamer.streaming(1));

  const auto editorRoot = root("editor");
  const auto report = terrain::saveProjectTerrains(editorRoot, registry, view);
  EXPECT_EQ(report.saved, 2u);
  EXPECT_TRUE(report.errors.empty());

  terrain::TerrainStore reloaded;
  reloaded.reconcile(registry);
  ASSERT_EQ(terrain::loadProjectTerrains(editorRoot, registry, reloaded).loaded, 2u);

  expectSameTerrain(original, reloaded, denseUuid);
  expectSameTerrain(original, reloaded, uniformUuid);
}

TEST_F(TerrainProjectSaveTest, TheEditorWriterRefusesAnIncompleteTerrainAndSavesOnceItIsComplete)
{
  AssetRegistry registry;
  terrain::TerrainStore store;
  fillSample(registry, store);
  const auto target = root("target");

  auto description = *store.description(denseUuid);
  description.chunkCount = static_cast<uint32_t>(store.find(denseUuid)->chunks().size()) + 1;
  ASSERT_TRUE(store.applyDescription(description));

  EXPECT_FALSE(writeEditorTerrains(target, registry, store));
  EXPECT_FALSE(std::filesystem::exists(target / "terrain"));

  // Positive control: once the store holds every chunk the description names, the same save writes both terrains.
  description.chunkCount = static_cast<uint32_t>(store.find(denseUuid)->chunks().size());
  ASSERT_TRUE(store.applyDescription(description));

  EXPECT_TRUE(writeEditorTerrains(target, registry, store));
  EXPECT_TRUE(std::filesystem::exists(terrain::terrainFolder(target, denseUuid) / terrain::indexFileName));
  EXPECT_TRUE(std::filesystem::exists(terrain::terrainFolder(target, uniformUuid) / terrain::indexFileName));
}

TEST_F(TerrainProjectSaveTest, TheEditorWriterLetsAProjectWithNoFolderSave)
{
  AssetRegistry registry;
  terrain::TerrainStore store;
  fillSample(registry, store);

  EXPECT_TRUE(writeEditorTerrains({}, registry, store));

  terrain::TerrainStore empty;
  empty.reconcile(registry);
  EXPECT_TRUE(writeEditorTerrains({}, registry, empty));

  // Positive control: the gate still refuses an incomplete terrain with no folder.
  auto description = *store.description(denseUuid);
  description.chunkCount = static_cast<uint32_t>(store.find(denseUuid)->chunks().size()) + 1;
  ASSERT_TRUE(store.applyDescription(description));
  EXPECT_FALSE(writeEditorTerrains({}, registry, store));
}

TEST_F(TerrainProjectSaveTest, TheEditorWriterFailsWhenATerrainCannotBeWritten)
{
  AssetRegistry registry;
  terrain::TerrainStore store;
  fillSample(registry, store);

  const auto file = m_base / "not-a-folder";
  std::ofstream(file) << "a file";

  EXPECT_FALSE(writeEditorTerrains(file, registry, store));

  // Positive control: a real folder saves.
  EXPECT_TRUE(writeEditorTerrains(root("real"), registry, store));
}

namespace {
  std::map<std::string, std::string> folderBytes(const std::filesystem::path& folder)
  {
    std::map<std::string, std::string> files;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder))
    {
      if (entry.is_regular_file())
      {
        std::ifstream in(entry.path(), std::ios::binary);
        files[std::filesystem::relative(entry.path(), folder).generic_string()] =
          std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
      }
    }

    return files;
  }
}

TEST_F(TerrainProjectSaveTest, AnEmptyVolumeOverSavedChunksKeepsTheFilesAndTheSaveSucceeds)
{
  AssetRegistry registry;
  terrain::TerrainStore store;
  fillSample(registry, store);
  const auto target = root("target");

  ASSERT_TRUE(writeEditorTerrains(target, registry, store));
  const auto denseFolder = terrain::terrainFolder(target, denseUuid);
  const auto before = folderBytes(denseFolder);
  ASSERT_GT(before.size(), 1u);

  terrain::TerrainStore reset;
  reset.reconcile(registry);
  ASSERT_TRUE(reset.adopt(describe(denseUuid, 2.5f), std::make_shared<terrain::TerrainVolume>(2.5f)));
  ASSERT_TRUE(reset.adopt(describe(uniformUuid, 4.0f), std::make_shared<terrain::TerrainVolume>(4.0f)));

  EXPECT_TRUE(writeEditorTerrains(target, registry, reset));
  EXPECT_EQ(folderBytes(denseFolder), before);
  EXPECT_GT(folderBytes(terrain::terrainFolder(target, uniformUuid)).size(), 1u);

  // Positive control: a volume that holds chunks still overwrites the saved files.
  auto changed = std::make_shared<terrain::TerrainVolume>(2.5f);
  changed->setChunk({ 5, 5, 5 }, terrainFilesFixtures::denseChunk(7));
  ASSERT_TRUE(reset.adopt(describe(denseUuid, 2.5f), changed));
  EXPECT_TRUE(writeEditorTerrains(target, registry, reset));
  EXPECT_NE(folderBytes(denseFolder), before);
  EXPECT_EQ(terrain::savedChunkCount(target, denseUuid), 1u);
}

TEST_F(TerrainProjectSaveTest, AnEmptyVolumeOverNoFilesWritesAnEmptyTerrain)
{
  AssetRegistry registry;
  terrain::TerrainStore store;
  registry.registerAsset(terrainRecord(denseUuid));
  ASSERT_TRUE(store.adopt(describe(denseUuid, 2.5f), std::make_shared<terrain::TerrainVolume>(2.5f)));
  const auto target = root("target");

  EXPECT_TRUE(writeEditorTerrains(target, registry, store));
  EXPECT_EQ(terrain::savedChunkCount(target, denseUuid), 0u);
  EXPECT_TRUE(std::filesystem::exists(terrain::terrainFolder(target, denseUuid) / terrain::indexFileName));
}

TEST_F(TerrainProjectSaveTest, SavedChunksToKeepNamesOnlyAnEmptyVolumeOverSavedChunks)
{
  AssetRegistry registry;
  terrain::TerrainStore store;
  fillSample(registry, store);
  const auto target = root("target");
  const terrain::TerrainVolume empty(2.5f);

  EXPECT_FALSE(savedChunksToKeep(target, denseUuid, empty).has_value());
  ASSERT_TRUE(writeEditorTerrains(target, registry, store));
  EXPECT_EQ(savedChunksToKeep(target, denseUuid, empty), 3u);
  EXPECT_FALSE(savedChunksToKeep(target, denseUuid, *store.find(denseUuid)).has_value());
  EXPECT_FALSE(savedChunksToKeep(target, unheldUuid, empty).has_value());
}

TEST_F(TerrainProjectSaveTest, TheFirstIncompleteTerrainNamesItsCounts)
{
  AssetRegistry registry;
  terrain::TerrainStore store;
  fillSample(registry, store);
  EXPECT_FALSE(terrain::firstIncompleteTerrain(store).has_value());

  auto description = *store.description(denseUuid);
  description.chunkCount = 5;
  ASSERT_TRUE(store.applyDescription(description));

  const auto incomplete = terrain::firstIncompleteTerrain(store);
  ASSERT_TRUE(incomplete.has_value());
  EXPECT_EQ(incomplete->uuid, denseUuid);
  EXPECT_EQ(incomplete->held, 3u);
  EXPECT_EQ(incomplete->expected, 5u);
}
