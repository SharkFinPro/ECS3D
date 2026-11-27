#include <gtest/gtest.h>

#include "TerrainAssetUndoFixtures.h"
#include "TerrainCreateFlow.h"
#include "TerrainStreamerFixtures.h"
#include "TestScene.h"
#include "AssetReferences.h"
#include "ComponentRegistry.h"
#include "Replication.h"
#include "assets/AssetRegistry.h"
#include "edits/EditCommand.h"
#include "edits/EditHistory.h"
#include "edits/RecordEdits.h"
#include "edits/RecordTerrainEdits.h"
#include "edits/TerrainRestore.h"
#include "objects/ObjectManager.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"
#include "terrain/TerrainAddition.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainReplication.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainUpload.h"
#include "terrain/TerrainVolume.h"

#include <Protocol.h>
#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <uuid.h>

using namespace terrainAssetUndoFixtures;
using namespace terrainStreamerFixtures;

TEST(TerrainAssetUndo, UndoOfADeleteRestoresEveryChunkBitExactlyOnTheServerAndRedoRemovesItAgain)
{
  World world;
  world.create();
  ASSERT_TRUE(world.serverHolds());

  const Hashes atDelete = sculptAfterCreate(world);
  ASSERT_EQ(atDelete.size(), static_cast<size_t>(createdChunks + 1));
  ASSERT_EQ(hashesOf(world.server.store, terrainId), atDelete);
  const auto descriptionAtDelete = *world.editor.store.description(terrainId);

  world.remove();
  EXPECT_FALSE(world.serverHolds());
  EXPECT_EQ(world.editor.store.find(terrainId), nullptr);

  const auto undone = world.undo();
  ASSERT_TRUE(undone.ok());
  ASSERT_TRUE(undone.terrainRestore.has_value());
  EXPECT_FALSE(undone.terrainRemoved.has_value());
  EXPECT_EQ(undone.terrainRestore->displayName, "Hills");
  EXPECT_EQ(undone.terrainRestore->chunks.size(), atDelete.size());
  world.apply(undone);

  // The server holds Play until the chunks land, and expects every one of them, the sculpted chunk included.
  ASSERT_TRUE(world.serverHolds());
  EXPECT_EQ(world.server.store.expectedChunks(terrainId), std::optional<uint32_t>(createdChunks + 1));
  EXPECT_TRUE(terrain::pendingUploadReason(world.server.store).has_value());
  EXPECT_TRUE(hashesOf(world.server.store, terrainId).empty());
  EXPECT_FALSE(world.upload.done());

  world.pumpUpload();

  EXPECT_FALSE(terrain::pendingUploadReason(world.server.store).has_value());
  EXPECT_EQ(hashesOf(world.server.store, terrainId), atDelete);
  EXPECT_EQ(hashesOf(world.editor.store, terrainId), atDelete);

  const auto* serverDescription = world.server.store.description(terrainId);
  ASSERT_NE(serverDescription, nullptr);
  EXPECT_EQ(serverDescription->uuid, descriptionAtDelete.uuid);
  EXPECT_EQ(serverDescription->voxelSize, descriptionAtDelete.voxelSize);
  EXPECT_EQ(serverDescription->palette, descriptionAtDelete.palette);
  EXPECT_EQ(serverDescription->chunkCount, static_cast<uint32_t>(createdChunks + 1));
  EXPECT_EQ(world.server.assets.getByUUID(terrainId)->displayName, "Hills");

  const auto redone = world.redo();
  ASSERT_TRUE(redone.ok());
  EXPECT_FALSE(redone.terrainRestore.has_value());
  EXPECT_EQ(redone.terrainRemoved, std::optional<uuids::uuid>(terrainId));
  world.apply(redone);
  EXPECT_FALSE(world.serverHolds());
  EXPECT_EQ(world.editor.store.find(terrainId), nullptr);

  const auto again = world.undo();
  ASSERT_TRUE(again.ok());
  world.apply(again);
  world.pumpUpload();
  EXPECT_EQ(hashesOf(world.server.store, terrainId), atDelete);
  EXPECT_FALSE(terrain::pendingUploadReason(world.server.store).has_value());
}

TEST(TerrainAssetUndo, UndoOfACreateRemovesTheTerrainAndRedoUploadsItsChunksAgain)
{
  World world;
  world.create();

  const auto created = hashesOf(world.server.store, terrainId);
  ASSERT_EQ(created.size(), static_cast<size_t>(createdChunks));
  ASSERT_EQ(hashesOf(world.editor.store, terrainId), created);
  EXPECT_EQ(world.history.nextUndoKind(), std::optional<edits::CommandKind>(edits::CommandKind::addAsset));

  const auto undone = world.undo();
  ASSERT_TRUE(undone.ok());
  EXPECT_FALSE(undone.terrainRestore.has_value());
  EXPECT_EQ(undone.terrainRemoved, std::optional<uuids::uuid>(terrainId));
  world.apply(undone);
  EXPECT_FALSE(world.serverHolds());
  EXPECT_EQ(world.server.assets.getByUUID(terrainId), nullptr);

  const auto redone = world.redo();
  ASSERT_TRUE(redone.ok());
  ASSERT_TRUE(redone.terrainRestore.has_value());
  world.apply(redone);
  ASSERT_TRUE(world.serverHolds());
  EXPECT_TRUE(terrain::pendingUploadReason(world.server.store).has_value());

  world.pumpUpload();
  EXPECT_EQ(hashesOf(world.server.store, terrainId), created);
  EXPECT_FALSE(terrain::pendingUploadReason(world.server.store).has_value());
  EXPECT_EQ(world.server.assets.getByUUID(terrainId)->displayName, "Hills");
}

TEST(TerrainAssetUndo, ACreatesTwoEntriesUndoTheObjectFirstThenTheTerrainAndRedoInTheOppositeOrder)
{
  World world;
  world.create();

  const auto edit = terrainCreate::placementEdit(placedObjectId, "Hills", terrainId);
  const auto commands = edits::commandsForSceneEdit(edit, *world.scene.objectManager);
  ASSERT_TRUE(commands.has_value());
  ASSERT_EQ(replication::applySceneEdit(*world.scene.objectManager, edit), replication::SceneEditResult::applied);
  world.history.recordBatch(*commands);

  const auto objectUndo = world.undo();
  ASSERT_TRUE(objectUndo.ok());
  ASSERT_TRUE(objectUndo.jsonPayload.has_value());
  EXPECT_FALSE(objectUndo.messagePayload.has_value());
  ASSERT_EQ(replication::applySceneEdit(*world.scene.objectManager, *objectUndo.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_TRUE(world.serverHolds());
  EXPECT_EQ(world.history.nextUndoKind(), std::optional<edits::CommandKind>(edits::CommandKind::addAsset));

  const auto assetUndo = world.undo();
  ASSERT_TRUE(assetUndo.ok());
  ASSERT_TRUE(assetUndo.messagePayload.has_value());
  world.apply(assetUndo);
  EXPECT_FALSE(world.serverHolds());

  const auto assetRedo = world.redo();
  ASSERT_TRUE(assetRedo.ok());
  EXPECT_EQ(world.history.nextRedoKind(), std::optional<edits::CommandKind>(edits::CommandKind::addComponent));
  world.apply(assetRedo);
  world.pumpUpload();
  EXPECT_TRUE(world.serverHolds());

  const auto objectRedo = world.redo();
  ASSERT_TRUE(objectRedo.ok());
  ASSERT_TRUE(objectRedo.jsonPayload.has_value());
  EXPECT_EQ(replication::applySceneEdit(*world.scene.objectManager, *objectRedo.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_NE(world.scene.objectManager->getObjectByUUID(placedObjectId), nullptr);
}

TEST(TerrainAssetUndo, ADescriptionWithAStaleChunkCountRestoresWithTheCountOfTheChunksHeld)
{
  terrain::TerrainDescription description;
  description.uuid = terrainId;
  description.chunkCount = 40;

  terrain::TerrainVolume::ChunkMap chunks;
  chunks[{ 0, 0, 0 }] = solid();
  chunks[{ 1, 0, 0 }] = noise(3);

  const auto restore = edits::makeTerrainRestore(description, chunks, "Name");
  EXPECT_EQ(restore.description.chunkCount, 2u);
  EXPECT_EQ(restore.chunks, chunks);
  ASSERT_TRUE(terrain::parseAddedTerrain(edits::terrainRestoreAssetJson(restore)).has_value());
  EXPECT_EQ(terrain::parseAddedTerrain(edits::terrainRestoreAssetJson(restore))->chunkCount, 2u);
  EXPECT_EQ(edits::terrainRestoreAssetJson(restore)["displayName"], "Name");
}

TEST(TerrainAssetUndo, RecordingNeedsTheStoreAndKeepsTodaysBehaviorWithoutOne)
{
  World world;
  world.create(false);
  const terrain::TerrainStore emptyStore;
  const auto removeOp = replication::buildRemoveAsset(terrainId);

  const auto withStore = edits::commandForRemoveAsset(removeOp, world.editor.assets, world.editor.scenes,
                                                       &world.editor.store);
  ASSERT_TRUE(withStore.has_value());
  EXPECT_EQ(withStore->kind(), edits::CommandKind::removeAsset);
  EXPECT_FALSE(edits::commandForRemoveAsset(removeOp, world.editor.assets, world.editor.scenes).has_value());
  EXPECT_FALSE(edits::commandForRemoveAsset(removeOp, world.editor.assets, world.editor.scenes, &emptyStore)
                 .has_value());

  // A terrain addAsset is never derived from the registry view: the create flow records commandForTerrainAdd.
  AssetRegistry freshAssets;
  EXPECT_FALSE(edits::commandForAddAsset(world.createAsset, freshAssets).has_value());
  EXPECT_TRUE(edits::commandForTerrainAdd(world.createAsset, world.editor.store).has_value());
  EXPECT_FALSE(edits::commandForTerrainAdd(world.createAsset, emptyStore).has_value());

  // A texture records without any store, as before.
  const nlohmann::json texture = { { "assetType", "texture" },
                                   { "uuid", "dddddddd-dddd-4ddd-8ddd-dddddddddddd" },
                                   { "path", "textures/a.png" } };
  EXPECT_TRUE(edits::commandForAddAsset(texture, freshAssets).has_value());
}

TEST(TerrainAssetUndo, UndoOfADeleteIsRefusedWhenTheStoreOrRegistryHoldsTheTerrainAgain)
{
  // Positive control: nothing holds it again, so the undo applies.
  {
    World world;
    world.create();
    world.remove();

    EXPECT_TRUE(world.undo().ok());
  }

  {
    World world;
    world.create();
    world.remove();
    ASSERT_NE(world.editor.store.create({ .uuid = terrainId, .voxelSize = 1.0f }), nullptr);

    const auto outcome = world.undo();
    EXPECT_EQ(outcome.result, edits::HistoryResult::targetChanged);
    EXPECT_EQ(outcome.conflict, std::optional<uuids::uuid>(terrainId));
    EXPECT_FALSE(world.history.canUndo());
  }

  {
    World world;
    world.create();
    world.remove();
    world.editor.assets.registerAsset({ .uuid = terrainId, .type = AssetType::Terrain,
                                        .path = terrainAssetPath(terrainId) });

    EXPECT_EQ(world.undo().result, edits::HistoryResult::targetChanged);
  }

  {
    World world;
    world.create();
    world.remove();

    const auto outcome = world.history.undo(*world.scene.objectManager, &world.editor.assets, nullptr, nullptr);
    EXPECT_EQ(outcome.result, edits::HistoryResult::targetMissing);
    EXPECT_EQ(outcome.conflict, std::optional<uuids::uuid>(terrainId));
  }
}

TEST(TerrainAssetUndo, RedoOfACreateIsRefusedWhenTheStoreHoldsTheTerrainAgain)
{
  {
    World world;
    world.create();
    world.apply(world.undo());

    EXPECT_TRUE(world.redo().ok());
  }

  World world;
  world.create();
  world.apply(world.undo());
  ASSERT_NE(world.editor.store.create({ .uuid = terrainId, .voxelSize = 1.0f }), nullptr);

  const auto outcome = world.redo();
  EXPECT_EQ(outcome.result, edits::HistoryResult::targetChanged);
  EXPECT_EQ(outcome.conflict, std::optional<uuids::uuid>(terrainId));
}

TEST(TerrainAssetUndo, TheEntriesNameTheTerrainInTheEditMenu)
{
  World world;
  world.create();
  EXPECT_EQ(world.history.nextUndoLabel(*world.scene.objectManager, &world.editor.assets),
            std::optional<std::string>("Add Asset Terrain bbbbbbbb"));

  world.remove();
  EXPECT_EQ(world.history.nextUndoLabel(*world.scene.objectManager, &world.editor.assets),
            std::optional<std::string>("Delete Asset Terrain bbbbbbbb"));
}

TEST(TerrainAssetUndo, AnObjectWithATerrainComponentCountsAsAReferenceToItsTerrainAsset)
{
  World world;
  world.create();
  const auto sceneAsset = std::make_shared<SceneAsset>(
    uuids::uuid::from_string("11111111-1111-1111-1111-111111111111").value(), "Scene", world.editor.components);
  world.editor.scenes.addScene(sceneAsset);
  world.editor.scenes.loadScene(sceneAsset);

  const auto scene = world.editor.scenes.getCurrentScene();
  ASSERT_NE(scene, nullptr);
  EXPECT_EQ(countAssetReferences(world.editor.scenes, world.editor.assets, terrainId), 0);

  ASSERT_EQ(replication::applySceneEdit(*scene->getObjectManager(),
                                        terrainCreate::placementEdit(placedObjectId, "Hills", terrainId)),
            replication::SceneEditResult::applied);
  EXPECT_EQ(countAssetReferences(world.editor.scenes, world.editor.assets, terrainId), 1);
}

TEST(TerrainAssetUndo, ADeleteOfATerrainTheEditorHoldsOnlyPartlyIsNotRecorded)
{
  World world;
  world.create(false);
  const auto removeOp = replication::buildRemoveAsset(terrainId);

  // Positive control: the same terrain, held completely, records.
  EXPECT_TRUE(edits::commandForRemoveAsset(removeOp, world.editor.assets, world.editor.scenes, &world.editor.store)
                .has_value());

  // A join stream or upload that has not landed leaves the volume short of the description's chunk count.
  world.editor.store.find(terrainId)->setChunk({ 0, 0, 0 }, nullptr);
  ASSERT_LT(world.editor.store.find(terrainId)->chunks().size(), world.editor.store.description(terrainId)->chunkCount);
  EXPECT_FALSE(edits::commandForRemoveAsset(removeOp, world.editor.assets, world.editor.scenes, &world.editor.store)
                 .has_value());

  // Once the missing chunk arrives it records again.
  world.editor.store.find(terrainId)->setChunk({ 0, 0, 0 }, noise(1));
  EXPECT_TRUE(edits::commandForRemoveAsset(removeOp, world.editor.assets, world.editor.scenes, &world.editor.store)
                .has_value());
}
