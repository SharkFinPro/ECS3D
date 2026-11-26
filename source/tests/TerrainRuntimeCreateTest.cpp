#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "ProjectPacker.h"
#include "Replication.h"
#include "assets/AssetRegistry.h"
#include "scenes/SceneManager.h"
#include "terrain/TerrainAddition.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainGenerators.h"
#include "terrain/TerrainOps.h"
#include "terrain/TerrainReplication.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainUpload.h"
#include "terrain/TerrainUploadOwners.h"
#include "terrain/TerrainVolume.h"

#include <Protocol.h>
#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <uuid.h>
#include <vector>

namespace {
  uuids::uuid uuidFrom(const std::string& text)
  {
    return uuids::uuid::from_string(text).value();
  }

  const auto terrainUUID = uuidFrom("11111111-1111-4111-8111-111111111111");
  const auto albedoUUID = uuidFrom("22222222-2222-4222-8222-222222222222");
  const auto specularUUID = uuidFrom("33333333-3333-4333-8333-333333333333");

  terrain::TerrainDescription sampleDescription(const uint32_t chunkCount)
  {
    terrain::TerrainDescription description;
    description.uuid = terrainUUID;
    description.voxelSize = 4.0f;
    description.palette.push_back({ "grass", albedoUUID, specularUUID });
    description.chunkCount = chunkCount;

    return description;
  }

  struct Side
  {
    std::shared_ptr<ComponentRegistry> componentRegistry = std::make_shared<ComponentRegistry>();
    AssetRegistry assetRegistry;
    SceneManager sceneManager;
    terrain::TerrainStore store;
    ProjectPacker packer;

    Side()
      : packer(&assetRegistry, &sceneManager, componentRegistry, &store)
    {
      registerDataComponents(*componentRegistry);
    }

    bool add(const nlohmann::json& asset, const bool expectUpload = true)
    {
      return replication::applyAddAssetWithTerrain(assetRegistry, sceneManager, componentRegistry, store, asset,
                                                   expectUpload);
    }
  };

  net::Message snapshotOf(const Side& side)
  {
    net::Message message(net::MessageType::snapshot);
    side.packer.pack(message);

    return message;
  }

  nlohmann::json bodyOf(const nlohmann::json& asset)
  {
    return nlohmann::json::parse(asset["body"].get<std::string>());
  }

  nlohmann::json withBody(const nlohmann::json& asset, const std::string& body)
  {
    nlohmann::json copy = asset;
    copy["body"] = body;

    return copy;
  }

  std::shared_ptr<terrain::TerrainVolume> flatSource()
  {
    auto source = std::make_shared<terrain::TerrainVolume>(4.0f);
    terrain::commit(*source, terrain::fillFlat(*source, { 64, 64 }, 10.0f, 20.0f, 1));

    return source;
  }
}

TEST(TerrainRuntimeCreate, TheBuilderJsonCreatesAnEmptyVolumeWithTheDescriptionAndAnExpectation)
{
  Side side;
  ASSERT_TRUE(side.add(terrain::addTerrainAssetJson(sampleDescription(6))));

  const auto* record = side.assetRegistry.getByUUIDOfType(terrainUUID, AssetType::Terrain);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->path, terrainAssetPath(terrainUUID));

  const auto volume = side.store.find(terrainUUID);
  ASSERT_NE(volume, nullptr);
  EXPECT_TRUE(volume->chunks().empty());
  EXPECT_FLOAT_EQ(volume->voxelSize(), 4.0f);

  const auto* description = side.store.description(terrainUUID);
  ASSERT_NE(description, nullptr);
  EXPECT_FLOAT_EQ(description->voxelSize, 4.0f);
  EXPECT_EQ(description->chunkCount, 6u);
  ASSERT_EQ(description->palette.size(), 1u);
  EXPECT_EQ(description->palette[0].name, "grass");
  EXPECT_EQ(description->palette[0].albedoTexture, albedoUUID);
  EXPECT_EQ(description->palette[0].specularTexture, specularUUID);
  EXPECT_EQ(side.store.expectedChunks(terrainUUID), std::optional<uint32_t>(6));
}

TEST(TerrainRuntimeCreate, AChunkCountOfZeroSetsNoExpectation)
{
  Side side;
  ASSERT_TRUE(side.add(terrain::addTerrainAssetJson(sampleDescription(0))));

  EXPECT_NE(side.store.find(terrainUUID), nullptr);
  EXPECT_FALSE(side.store.expectedChunks(terrainUUID).has_value());
}

TEST(TerrainRuntimeCreate, ATerrainAddAssetWithoutABodyOnlyRegistersTheRecord)
{
  Side side;
  nlohmann::json asset = terrain::addTerrainAssetJson(sampleDescription(3));
  asset.erase("body");

  EXPECT_TRUE(side.add(asset));
  EXPECT_NE(side.assetRegistry.getByUUIDOfType(terrainUUID, AssetType::Terrain), nullptr);
  EXPECT_EQ(side.store.find(terrainUUID), nullptr);
}

TEST(TerrainRuntimeCreate, ARefusedDescriptionLeavesRegistryAndStoreUntouched)
{
  const auto valid = terrain::addTerrainAssetJson(sampleDescription(2));

  Side control;
  ASSERT_TRUE(control.add(valid));
  ASSERT_NE(control.store.find(terrainUUID), nullptr);

  auto badVoxel = bodyOf(valid);
  badVoxel["voxelSize"] = 0.0;

  auto bigPalette = bodyOf(valid);
  bigPalette["palette"] = nlohmann::json::array();
  for (size_t i = 0; i <= terrain::maxPaletteSize; ++i)
  {
    bigPalette["palette"].push_back({ { "name", "m" },
                                      { "albedoTexture", uuids::to_string(albedoUUID) },
                                      { "specularTexture", uuids::to_string(specularUUID) } });
  }

  auto tooManyChunks = bodyOf(valid);
  tooManyChunks["chunkCount"] = static_cast<uint64_t>(terrain::maxAddedTerrainChunks) + 1;

  auto badMaterial = bodyOf(valid);
  badMaterial["palette"][0]["albedoTexture"] = "not a uuid";

  const std::vector<nlohmann::json> refused = {
    withBody(valid, badVoxel.dump()),
    withBody(valid, bigPalette.dump()),
    withBody(valid, tooManyChunks.dump()),
    withBody(valid, badMaterial.dump()),
    withBody(valid, "{\"voxelSize\":"),
    withBody(valid, "[1,2]"),
  };

  for (const nlohmann::json& asset : refused)
  {
    Side side;
    EXPECT_FALSE(side.add(asset)) << asset["body"];
    EXPECT_EQ(side.assetRegistry.getByUUID(terrainUUID), nullptr);
    EXPECT_TRUE(side.store.terrains().empty());
  }
}

TEST(TerrainRuntimeCreate, AUUIDTheStoreAlreadyHoldsIsRefusedAndItsVolumeKept)
{
  Side side;
  ASSERT_TRUE(side.add(terrain::addTerrainAssetJson(sampleDescription(2))));

  const auto held = side.store.find(terrainUUID);
  held->setChunk({ 0, 0, 0 }, terrain::TerrainChunk::uniform(-terrain::airDistance, 1));

  auto again = sampleDescription(9);
  again.voxelSize = 8.0f;
  EXPECT_FALSE(side.add(terrain::addTerrainAssetJson(again)));

  EXPECT_EQ(side.store.find(terrainUUID), held);
  EXPECT_EQ(held->chunks().size(), 1u);
  EXPECT_FLOAT_EQ(side.store.description(terrainUUID)->voxelSize, 4.0f);
  EXPECT_EQ(side.store.expectedChunks(terrainUUID), std::optional<uint32_t>(2));
}

TEST(TerrainRuntimeCreate, AnUploadOfAGeneratedVolumeCompletesTheExpectationAndMatchesTheSource)
{
  const auto source = flatSource();
  ASSERT_FALSE(source->chunks().empty());

  Side server;
  ASSERT_TRUE(server.add(
    terrain::addTerrainAssetJson(sampleDescription(static_cast<uint32_t>(source->chunks().size())))));
  EXPECT_TRUE(terrain::pendingUploadReason(server.store).has_value());

  terrain::TerrainUpload upload;
  upload.add(terrainUUID, source);

  bool completed = false;
  upload.pump([&](const net::Message& message) {
    const auto payload = terrain::unpackTerrainChunks(message);
    ASSERT_TRUE(payload.has_value());
    completed = terrain::applyTerrainUpload(server.store, *payload).completed;
  }, 100u * 1024 * 1024);

  EXPECT_TRUE(upload.done());
  EXPECT_TRUE(completed);
  EXPECT_FALSE(server.store.expectedChunks(terrainUUID).has_value());
  EXPECT_FALSE(terrain::pendingUploadReason(server.store).has_value());

  const auto held = server.store.find(terrainUUID);
  ASSERT_EQ(held->chunks().size(), source->chunks().size());
  for (const auto& [coord, chunk] : source->chunks())
  {
    const auto got = held->chunk(coord);
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->hash(), chunk->hash());
  }
}

TEST(TerrainRuntimeCreate, AnUploadBeforeTheAddAssetHasNowhereToGo)
{
  terrain::TerrainUpload upload;
  upload.add(terrainUUID, flatSource());

  Side server;
  bool unknown = false;
  upload.pump([&](const net::Message& message) {
    const auto payload = terrain::unpackTerrainChunks(message);
    ASSERT_TRUE(payload.has_value());
    unknown = terrain::applyTerrainUpload(server.store, *payload).unknownTerrain;
  }, 100u * 1024 * 1024);

  EXPECT_TRUE(unknown);
}

TEST(TerrainRuntimeCreate, ASnapshotAfterTheCreateTeachesASecondViewTheDescriptionAndTheTarget)
{
  Side server;
  ASSERT_TRUE(server.add(terrain::addTerrainAssetJson(sampleDescription(5))));

  Side view;
  ASSERT_EQ(view.store.find(terrainUUID), nullptr);
  view.packer.unpack(snapshotOf(server));

  const auto* description = view.store.description(terrainUUID);
  ASSERT_NE(description, nullptr);
  EXPECT_FLOAT_EQ(description->voxelSize, 4.0f);
  EXPECT_EQ(description->chunkCount, 5u);
  ASSERT_EQ(description->palette.size(), 1u);
  EXPECT_EQ(description->palette[0].albedoTexture, albedoUUID);
  EXPECT_TRUE(view.store.find(terrainUUID)->chunks().empty());
  EXPECT_TRUE(terrain::incompleteTerrainReason(view.store).has_value());
}

TEST(TerrainRuntimeCreate, RemovingTheAssetDropsTheTerrainAndItsExpectationOnReconcile)
{
  Side side;
  ASSERT_TRUE(side.add(terrain::addTerrainAssetJson(sampleDescription(5))));
  side.store.reconcile(side.assetRegistry);
  ASSERT_NE(side.store.find(terrainUUID), nullptr);
  ASSERT_TRUE(side.store.expectedChunks(terrainUUID).has_value());

  ASSERT_TRUE(replication::applyRemoveAsset(side.assetRegistry, side.sceneManager,
                                            replication::buildRemoveAsset(terrainUUID)));
  side.store.reconcile(side.assetRegistry);

  EXPECT_EQ(side.store.find(terrainUUID), nullptr);
  EXPECT_FALSE(side.store.expectedChunks(terrainUUID).has_value());
  EXPECT_FALSE(terrain::pendingUploadReason(side.store).has_value());
}

TEST(TerrainRuntimeCreate, AWronglyTypedOrOutOfRangeFieldIsRefusedWithRegistryAndStoreUntouched)
{
  const auto valid = terrain::addTerrainAssetJson(sampleDescription(2));

  auto negativeCount = bodyOf(valid);
  negativeCount["chunkCount"] = -1;

  auto floatCount = bodyOf(valid);
  floatCount["chunkCount"] = 2.5;

  auto stringVoxel = bodyOf(valid);
  stringVoxel["voxelSize"] = "4";

  auto hugeVoxel = bodyOf(valid);
  hugeVoxel["voxelSize"] = 1.0e300;

  auto numberName = bodyOf(valid);
  numberName["palette"][0]["name"] = 7;

  auto noPalette = bodyOf(valid);
  noPalette.erase("palette");

  std::vector<nlohmann::json> refused = {
    withBody(valid, negativeCount.dump()),
    withBody(valid, floatCount.dump()),
    withBody(valid, stringVoxel.dump()),
    withBody(valid, hugeVoxel.dump()),
    withBody(valid, numberName.dump()),
    withBody(valid, noPalette.dump()),
  };

  auto nilUUID = valid;
  nilUUID["uuid"] = uuids::to_string(uuids::uuid{});
  nilUUID["path"] = terrainAssetPath(uuids::uuid{});
  refused.push_back(nilUUID);

  Side control;
  EXPECT_TRUE(control.add(valid));

  for (const nlohmann::json& asset : refused)
  {
    Side side;
    EXPECT_FALSE(side.add(asset)) << asset.dump();
    EXPECT_TRUE(side.assetRegistry.getAssets().empty()) << asset.dump();
    EXPECT_TRUE(side.store.terrains().empty()) << asset.dump();
  }
}

TEST(TerrainRuntimeCreate, AUUIDTheRegistryHoldsAsAnotherTypeIsRefusedAndNothingChanges)
{
  Side side;
  side.assetRegistry.registerAsset({ .uuid = terrainUUID, .type = AssetType::Model, .path = "rock.obj" });
  ASSERT_NE(side.assetRegistry.getByUUIDOfType(terrainUUID, AssetType::Model), nullptr);

  EXPECT_FALSE(side.add(terrain::addTerrainAssetJson(sampleDescription(2))));
  EXPECT_NE(side.assetRegistry.getByUUIDOfType(terrainUUID, AssetType::Model), nullptr);
  EXPECT_EQ(side.assetRegistry.getByUUIDOfType(terrainUUID, AssetType::Terrain), nullptr);
  EXPECT_TRUE(side.store.terrains().empty());

  auto other = sampleDescription(2);
  other.uuid = albedoUUID;
  EXPECT_TRUE(side.add(terrain::addTerrainAssetJson(other)));
  EXPECT_NE(side.store.find(albedoUUID), nullptr);
}

TEST(TerrainRuntimeCreate, ARecordThatRegistrationDropsLeavesTheStoreUntouched)
{
  auto asset = terrain::addTerrainAssetJson(sampleDescription(2));
  asset["path"] = "terrain/not-the-canonical-path";

  Side side;
  EXPECT_FALSE(side.add(asset));
  EXPECT_EQ(side.assetRegistry.getByUUID(terrainUUID), nullptr);
  EXPECT_TRUE(side.store.terrains().empty());
}

TEST(TerrainRuntimeCreate, OnlyTheServerSideApplyRecordsTheExpectationAndTheCreatedDescriptionIsReported)
{
  Side editor;
  ASSERT_TRUE(editor.add(terrain::addTerrainAssetJson(sampleDescription(3)), false));
  EXPECT_NE(editor.store.find(terrainUUID), nullptr);
  EXPECT_EQ(editor.store.description(terrainUUID)->chunkCount, 3u);
  EXPECT_FALSE(editor.store.expectedChunks(terrainUUID).has_value());

  Side server;
  std::optional<terrain::TerrainDescription> created;
  ASSERT_TRUE(replication::applyAddAssetWithTerrain(server.assetRegistry, server.sceneManager,
                                                    server.componentRegistry, server.store,
                                                    terrain::addTerrainAssetJson(sampleDescription(3)), true,
                                                    &created));
  EXPECT_EQ(server.store.expectedChunks(terrainUUID), std::optional<uint32_t>(3));
  ASSERT_TRUE(created.has_value());
  EXPECT_EQ(created->uuid, terrainUUID);
  EXPECT_EQ(created->chunkCount, 3u);
}

namespace {
  const auto otherTerrain = uuidFrom("44444444-4444-4444-8444-444444444444");

  void expectBoth(terrain::TerrainStore& store)
  {
    ASSERT_NE(store.create({ .uuid = terrainUUID }), nullptr);
    ASSERT_NE(store.create({ .uuid = otherTerrain }), nullptr);
    ASSERT_TRUE(store.expectChunks(terrainUUID, 4));
    ASSERT_TRUE(store.expectChunks(otherTerrain, 4));
  }
}

TEST(TerrainUploadOwners, ACreatorsDisconnectDropsItsTerrainAndLeavesTheLoadersAlone)
{
  terrain::TerrainStore store;
  expectBoth(store);

  terrain::TerrainUploadOwners owners;
  owners.claim(terrainUUID, 1);
  owners.claim(otherTerrain, 2);

  const auto dropped = owners.drop(2, store);

  ASSERT_EQ(dropped.size(), 1u);
  EXPECT_EQ(dropped[0], otherTerrain);
  EXPECT_FALSE(store.expectedChunks(otherTerrain).has_value());
  EXPECT_TRUE(store.expectedChunks(terrainUUID).has_value());

  EXPECT_EQ(owners.drop(1, store), std::vector<uuids::uuid>{ terrainUUID });
  EXPECT_FALSE(store.expectedChunks(terrainUUID).has_value());
}

TEST(TerrainUploadOwners, TheLoadersDisconnectLeavesTheCreatorsUploadAccepted)
{
  terrain::TerrainStore store;
  expectBoth(store);

  terrain::TerrainUploadOwners owners;
  owners.claim(terrainUUID, 1);
  owners.claim(otherTerrain, 2);

  EXPECT_EQ(owners.drop(1, store), std::vector<uuids::uuid>{ terrainUUID });
  EXPECT_TRUE(store.expectedChunks(otherTerrain).has_value());

  terrain::TerrainChunksPayload payload;
  payload.terrain = otherTerrain;
  const auto chunk = terrain::TerrainChunk::uniform(-terrain::airDistance, 1);
  payload.entries.push_back({ { 0, 0, 0 }, 1, chunk->hash(), chunk });

  const auto result = terrain::applyTerrainUpload(store, payload);
  EXPECT_FALSE(result.notExpected);
  EXPECT_EQ(result.applied.size(), 1u);
}

TEST(TerrainUploadOwners, ACompletedOrRemovedTerrainIsNotDroppedAgain)
{
  terrain::TerrainStore store;
  expectBoth(store);

  terrain::TerrainUploadOwners owners;
  owners.claimExpected(store, 7);
  EXPECT_EQ(owners.owner(terrainUUID), std::optional<int32_t>(7));
  EXPECT_EQ(owners.owner(otherTerrain), std::optional<int32_t>(7));

  owners.release(terrainUUID);
  EXPECT_FALSE(owners.owner(terrainUUID).has_value());
  store.clearExpectation(terrainUUID);
  ASSERT_TRUE(store.expectChunks(terrainUUID, 9));

  store.remove(otherTerrain);

  EXPECT_TRUE(owners.drop(7, store).empty());
  EXPECT_EQ(store.expectedChunks(terrainUUID), std::optional<uint32_t>(9));
  EXPECT_FALSE(owners.owner(otherTerrain).has_value());

  owners.claim(terrainUUID, 8);
  owners.clear();
  EXPECT_TRUE(owners.drop(8, store).empty());
}
