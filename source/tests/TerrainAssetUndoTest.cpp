#include <gtest/gtest.h>

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

using namespace terrainStreamerFixtures;

namespace {
  const auto terrainId = uuids::uuid::from_string("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb").value();
  const auto placedObjectId = uuids::uuid::from_string("cccccccc-cccc-4ccc-8ccc-cccccccccccc").value();
  constexpr int createdChunks = 4;
  constexpr size_t unlimitedBudget = 100u * 1024 * 1024;

  using Hashes = std::map<std::tuple<int32_t, int32_t, int32_t>, uint64_t>;

  Hashes hashesOf(const terrain::TerrainStore& store, const uuids::uuid& uuid)
  {
    Hashes hashes;

    if (const auto volume = store.find(uuid))
    {
      for (const auto& [coord, chunk] : volume->chunks())
      {
        hashes[{ coord.x, coord.y, coord.z }] = chunk->hash();
      }
    }

    return hashes;
  }

  // One side of the wire: the registries and store an editor or the server holds.
  struct Side
  {
    std::shared_ptr<ComponentRegistry> components;
    AssetRegistry assets;
    SceneManager scenes;
    terrain::TerrainStore store;
  };

  // The editor, the server it talks to and the history the editor records. The editor applies every op locally and
  // sends it; the server applies the wire copy, with the upload expectation, and takes the uploaded chunks.
  struct World
  {
    fixtures::Scene scene;
    Side editor;
    Side server;
    edits::EditHistory history;
    terrain::TerrainUpload upload;
    nlohmann::json createAsset;

    World()
    {
      editor.components = scene.componentRegistry;
      server.components = scene.componentRegistry;
    }

    // The create flow: the asset record and an empty volume first, then the generated chunks into the store and onto
    // the upload queue, then the command recorded from the store.
    void create(const bool record = true, const bool pump = true, const int chunkCount = createdChunks)
    {
      terrain::TerrainDescription description;
      description.uuid = terrainId;
      description.voxelSize = 1.0f;
      description.chunkCount = static_cast<uint32_t>(chunkCount);

      auto generated = std::make_shared<terrain::TerrainVolume>(1.0f);
      for (int x = 0; x < chunkCount; ++x)
      {
        generated->setChunk({ x, 0, 0 }, noise(static_cast<uint32_t>(x + 1)));
      }

      createAsset = terrain::addTerrainAssetJson(description);
      createAsset["displayName"] = "Hills";

      ASSERT_TRUE(replication::applyAddAssetWithTerrain(editor.assets, editor.scenes, editor.components, editor.store,
                                                        createAsset, false));
      editor.store.find(terrainId)->restoreChunks(generated->snapshotChunks());
      upload.add(terrainId, generated);

      if (record)
      {
        const auto command = edits::commandForTerrainAdd(createAsset, editor.store);
        ASSERT_TRUE(command.has_value());
        history.record(*command);
      }

      sendAddAsset(replication::packAddAsset(createAsset));

      if (pump)
      {
        pumpUpload();
      }
    }

    void sendAddAsset(const net::Message& message)
    {
      const auto asset = replication::unpackAddAsset(message);
      EXPECT_TRUE(replication::applyAddAssetWithTerrain(server.assets, server.scenes, server.components, server.store,
                                                        asset, true));
    }

    void sendRemoveAsset(const net::Message& message)
    {
      EXPECT_TRUE(replication::applyRemoveAsset(server.assets, server.scenes, replication::unpackRemoveAsset(message)));
      server.store.reconcile(server.assets);
    }

    void pumpUpload()
    {
      upload.pump([this](const net::Message& message) {
        const auto payload = terrain::unpackTerrainChunks(message);
        ASSERT_TRUE(payload.has_value());
        static_cast<void>(terrain::applyTerrainUpload(server.store, *payload));
      }, unlimitedBudget);
    }

    // What EditorApp::onRemoveAsset does: record from the store while it still holds the terrain, apply, send.
    void remove()
    {
      const auto op = replication::buildRemoveAsset(terrainId);
      const auto command = edits::commandForRemoveAsset(op, editor.assets, editor.scenes, &editor.store);
      ASSERT_TRUE(command.has_value());

      ASSERT_TRUE(replication::applyRemoveAsset(editor.assets, editor.scenes, op));
      editor.store.reconcile(editor.assets);
      history.record(*command);

      sendRemoveAsset(replication::packRemoveAsset(op));
    }

    [[nodiscard]] edits::HistoryOutcome undo()
    {
      return history.undo(*scene.objectManager, &editor.assets, nullptr, &editor.store);
    }

    [[nodiscard]] edits::HistoryOutcome redo()
    {
      return history.redo(*scene.objectManager, &editor.assets, nullptr, &editor.store);
    }

    // What EditorApp::reportHistoryOutcome does for an asset payload.
    void apply(const edits::HistoryOutcome& outcome)
    {
      ASSERT_TRUE(outcome.ok());
      ASSERT_TRUE(outcome.messagePayload.has_value());

      if (outcome.terrainRestore)
      {
        ASSERT_EQ(edits::restoreTerrainAndSend(editor.assets, editor.scenes, editor.components, editor.store, upload,
                                               *outcome.terrainRestore,
                                               [&] { sendAddAsset(*outcome.messagePayload); }),
                  edits::TerrainRestoreResult::applied);
        return;
      }

      if (outcome.terrainRemoved)
      {
        upload.forget(*outcome.terrainRemoved);
      }

      if (outcome.messagePayload->getType() == net::MessageType::addAsset)
      {
        sendAddAsset(*outcome.messagePayload);
      }
      else
      {
        // The editor does not apply a removal itself: the snapshot that follows the server's does.
        ASSERT_TRUE(replication::applyRemoveAsset(editor.assets, editor.scenes,
                                                  replication::unpackRemoveAsset(*outcome.messagePayload)));
        editor.store.reconcile(editor.assets);
        sendRemoveAsset(*outcome.messagePayload);
      }
    }

    [[nodiscard]] bool serverHolds() const
    {
      return server.store.find(terrainId) != nullptr && server.assets.getByUUID(terrainId) != nullptr;
    }
  };

  // A chunk set after the create, so the state at delete time differs from what the create generated.
  Hashes sculptAfterCreate(World& world)
  {
    const auto edited = noise(99);
    world.editor.store.find(terrainId)->setChunk({ createdChunks, 0, 0 }, edited);
    world.server.store.setChunk(terrainId, { createdChunks, 0, 0 }, edited, world.server.store.nextRevision(terrainId));

    return hashesOf(world.editor.store, terrainId);
  }
}

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

TEST(TerrainAssetUndo, ARestoreThatThrowsWhileApplyingRollsTheRegistryStoreAndQueueBack)
{
  World world;
  terrain::TerrainDescription description;
  description.uuid = terrainId;
  description.voxelSize = 1.0f;

  terrain::TerrainVolume::ChunkMap chunks;
  chunks[{ 0, 0, 0 }] = noise(1);

  int sends = 0;
  const auto countSend = [&sends] { ++sends; };

  // Positive control: a valid restore applies, sends once and queues the chunk.
  const auto restore = edits::makeTerrainRestore(description, chunks, "Hills");
  EXPECT_EQ(edits::restoreTerrainAndSend(world.editor.assets, world.editor.scenes, world.editor.components,
                                         world.editor.store, world.upload, restore, countSend),
            edits::TerrainRestoreResult::applied);
  EXPECT_EQ(sends, 1);
  EXPECT_NE(world.editor.store.find(terrainId), nullptr);
  EXPECT_NE(world.editor.assets.getByUUID(terrainId), nullptr);
  EXPECT_EQ(world.upload.pendingChunks(), 1u);

  World broken;
  terrain::TerrainVolume::ChunkMap invalid;
  invalid[{ terrain::maxChunkCoord + 1, 0, 0 }] = noise(2);
  const auto badRestore = edits::makeTerrainRestore(description, invalid, "Hills");

  sends = 0;
  EXPECT_EQ(edits::restoreTerrainAndSend(broken.editor.assets, broken.editor.scenes, broken.editor.components,
                                         broken.editor.store, broken.upload, badRestore, countSend),
            edits::TerrainRestoreResult::failed);
  EXPECT_EQ(sends, 0);
  EXPECT_EQ(broken.editor.store.find(terrainId), nullptr);
  EXPECT_EQ(broken.editor.assets.getByUUID(terrainId), nullptr);
  EXPECT_TRUE(broken.upload.done());
}

TEST(TerrainAssetUndo, ARestoreWhoseSendThrowsRollsBackAndQueuesNothing)
{
  World world;
  terrain::TerrainDescription description;
  description.uuid = terrainId;
  description.voxelSize = 1.0f;

  terrain::TerrainVolume::ChunkMap chunks;
  chunks[{ 0, 0, 0 }] = noise(1);
  const auto restore = edits::makeTerrainRestore(description, chunks, "Hills");

  EXPECT_EQ(edits::restoreTerrainAndSend(world.editor.assets, world.editor.scenes, world.editor.components,
                                         world.editor.store, world.upload, restore,
                                         [] { throw std::runtime_error("disconnected"); }),
            edits::TerrainRestoreResult::failed);
  EXPECT_EQ(world.editor.store.find(terrainId), nullptr);
  EXPECT_EQ(world.editor.assets.getByUUID(terrainId), nullptr);
  EXPECT_TRUE(world.upload.done());

  // Positive control: the same restore applies cleanly once the send works.
  EXPECT_EQ(edits::restoreTerrainAndSend(world.editor.assets, world.editor.scenes, world.editor.components,
                                         world.editor.store, world.upload, restore, [] {}),
            edits::TerrainRestoreResult::applied);
  EXPECT_FALSE(world.upload.done());
}

TEST(TerrainAssetUndo, ARefusedRestoreLeavesTheTerrainTheEditorAlreadyHoldsAlone)
{
  World world;
  world.create(false);
  const Hashes before = hashesOf(world.editor.store, terrainId);
  ASSERT_FALSE(before.empty());

  const terrain::TerrainDescription description = *world.editor.store.description(terrainId);
  terrain::TerrainVolume::ChunkMap chunks;
  chunks[{ 0, 0, 0 }] = noise(77);
  const auto restore = edits::makeTerrainRestore(description, chunks, "Hills");

  int sends = 0;
  EXPECT_EQ(edits::restoreTerrainAndSend(world.editor.assets, world.editor.scenes, world.editor.components,
                                         world.editor.store, world.upload, restore, [&sends] { ++sends; }),
            edits::TerrainRestoreResult::refused);
  EXPECT_EQ(sends, 0);
  EXPECT_EQ(hashesOf(world.editor.store, terrainId), before);
  EXPECT_NE(world.editor.assets.getByUUID(terrainId), nullptr);
}

TEST(TerrainAssetUndo, UndoOfACreateWhileItsUploadIsStillQueuedDropsTheQueue)
{
  World world;
  world.create(true, false);
  ASSERT_EQ(world.upload.pendingChunks(), static_cast<size_t>(createdChunks));

  const auto undone = world.undo();
  ASSERT_TRUE(undone.ok());
  ASSERT_EQ(undone.terrainRemoved, std::optional<uuids::uuid>(terrainId));
  world.apply(undone);

  EXPECT_TRUE(world.upload.done());
  EXPECT_EQ(world.upload.pendingChunks(), 0u);
  EXPECT_TRUE(world.upload.terrains().empty());
  EXPECT_FALSE(world.serverHolds());
  EXPECT_TRUE(hashesOf(world.server.store, terrainId).empty());
}

TEST(TerrainAssetUndo, AZeroChunkTerrainDeletesAndRestoresWithNothingToUpload)
{
  World world;
  world.create(true, true, 0);
  ASSERT_TRUE(world.serverHolds());
  ASSERT_TRUE(world.upload.done());

  world.remove();
  EXPECT_FALSE(world.serverHolds());
  EXPECT_EQ(world.editor.store.find(terrainId), nullptr);

  const auto undone = world.undo();
  ASSERT_TRUE(undone.ok());
  ASSERT_TRUE(undone.terrainRestore.has_value());
  EXPECT_TRUE(undone.terrainRestore->chunks.empty());
  world.apply(undone);

  EXPECT_NE(world.editor.store.find(terrainId), nullptr);
  EXPECT_NE(world.editor.assets.getByUUID(terrainId), nullptr);
  EXPECT_TRUE(world.upload.done());
  EXPECT_EQ(world.upload.pendingChunks(), 0u);
  EXPECT_TRUE(world.editor.store.find(terrainId)->chunks().empty());
}

TEST(TerrainAssetUndo, AStrokeRecordedBeforeADeleteUndoesAfterTheDeleteIsUndone)
{
  World world;
  world.create();

  const auto volume = world.editor.store.find(terrainId);
  ASSERT_NE(volume, nullptr);
  const auto original = volume->chunk({ 0, 0, 0 });
  ASSERT_NE(original, nullptr);
  const uint64_t originalHash = original->hash();

  const std::vector<std::pair<terrain::ChunkCoord, std::shared_ptr<const terrain::TerrainChunk>>> before{
    { { 0, 0, 0 }, original }
  };
  volume->setChunk({ 0, 0, 0 }, noise(50));
  world.server.store.setChunk(terrainId, { 0, 0, 0 }, noise(50), world.server.store.nextRevision(terrainId));

  auto stroke = edits::commandForTerrainStroke(terrainId, before, *volume);
  ASSERT_TRUE(stroke.has_value());
  world.history.record(std::move(*stroke));

  const Hashes atDelete = hashesOf(world.editor.store, terrainId);
  world.remove();
  world.apply(world.undo());
  world.pumpUpload();
  ASSERT_EQ(hashesOf(world.editor.store, terrainId), atDelete);
  ASSERT_EQ(hashesOf(world.server.store, terrainId), atDelete);

  const auto strokeUndo = world.undo();
  ASSERT_TRUE(strokeUndo.ok());
  ASSERT_TRUE(strokeUndo.terrainPayload.has_value());
  ASSERT_EQ(strokeUndo.terrainPayload->chunks.size(), 1u);
  EXPECT_EQ(strokeUndo.terrainPayload->chunks[0].coord, (terrain::ChunkCoord{ 0, 0, 0 }));
  ASSERT_NE(strokeUndo.terrainPayload->chunks[0].chunk, nullptr);
  EXPECT_EQ(strokeUndo.terrainPayload->chunks[0].chunk->hash(), originalHash);

  edits::predictSetChunks(*world.editor.store.find(terrainId), *strokeUndo.terrainPayload);
  EXPECT_EQ(world.editor.store.find(terrainId)->chunk({ 0, 0, 0 })->hash(), originalHash);
}

TEST(TerrainAssetUndo, ATerrainCommandRefusesARestoreThatNamesAnotherUuid)
{
  terrain::TerrainDescription description;
  description.uuid = terrainId;
  const terrain::TerrainVolume::ChunkMap chunks;
  const auto restore = edits::makeTerrainRestore(description, chunks, "Hills");
  const auto other = uuids::uuid::from_string("dddddddd-dddd-4ddd-8ddd-dddddddddddd").value();

  EXPECT_NO_THROW(static_cast<void>(
    edits::EditCommand::addAsset(terrainId, AssetType::Terrain, terrainAssetPath(terrainId), "", "", restore)));
  EXPECT_NO_THROW(static_cast<void>(edits::EditCommand::removeAsset(terrainId, AssetType::Terrain,
                                                                    terrainAssetPath(terrainId), "", "", "Hills",
                                                                    restore)));
  EXPECT_THROW(static_cast<void>(edits::EditCommand::addAsset(other, AssetType::Terrain, terrainAssetPath(other), "",
                                                              "", restore)),
               std::invalid_argument);
  EXPECT_THROW(static_cast<void>(edits::EditCommand::removeAsset(other, AssetType::Terrain, terrainAssetPath(other),
                                                                 "", "", "Hills", restore)),
               std::invalid_argument);
}

TEST(TerrainAssetUndo, ATerrainAssetCommandPinsTheBytesOfTheChunksItHolds)
{
  terrain::TerrainDescription description;
  description.uuid = terrainId;
  terrain::TerrainVolume::ChunkMap chunks;
  chunks[{ 0, 0, 0 }] = solid();
  chunks[{ 1, 0, 0 }] = noise(3);
  const auto restore = edits::makeTerrainRestore(description, chunks, "Hills");
  const std::size_t expected = solid()->byteSize() + noise(3)->byteSize();
  ASSERT_GT(expected, 0u);

  const auto add =
    edits::EditCommand::addAsset(terrainId, AssetType::Terrain, terrainAssetPath(terrainId), "", "", restore);
  const auto remove = edits::EditCommand::removeAsset(terrainId, AssetType::Terrain, terrainAssetPath(terrainId), "",
                                                      "", "Hills", restore);
  EXPECT_EQ(add.pinnedBytes(), expected);
  EXPECT_EQ(remove.pinnedBytes(), expected);

  const auto bare = edits::EditCommand::removeAsset(terrainId, AssetType::Terrain, terrainAssetPath(terrainId), "",
                                                    "", "Hills");
  EXPECT_EQ(bare.pinnedBytes(), 0u);

  edits::EditHistory history(expected - 1);
  history.record(remove);
  history.record(add);
  EXPECT_EQ(history.undoDepth(), 1u);
  EXPECT_EQ(history.pinnedBytes(), expected);
}
