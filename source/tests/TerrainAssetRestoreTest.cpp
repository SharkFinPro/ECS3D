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
