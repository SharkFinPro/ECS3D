#include <gtest/gtest.h>

#include "TestScene.h"
#include "edits/EditCommand.h"
#include "edits/EditHistory.h"
#include "edits/RecordTerrainEdits.h"
#include "edits/TerrainSetChunks.h"
#include "objects/ObjectManager.h"
#include "terrain/TerrainBrush.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainCodec.h"
#include "terrain/TerrainEdit.h"
#include "terrain/TerrainEditApply.h"
#include "terrain/TerrainOps.h"
#include "terrain/TerrainReplication.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <uuid.h>
#include <vector>

namespace {
  const auto terrainId = uuids::uuid::from_string("11111111-1111-1111-1111-111111111111").value();

  using Snapshot = std::vector<std::tuple<int32_t, int32_t, int32_t, uint64_t>>;
  using ChunkPtr = std::shared_ptr<const terrain::TerrainChunk>;
  using BeforeList = std::vector<std::pair<terrain::ChunkCoord, ChunkPtr>>;

  Snapshot snapshotOf(const terrain::TerrainVolume& volume)
  {
    Snapshot snapshot;

    for (const auto& [coord, chunk] : volume.chunks())
    {
      snapshot.emplace_back(coord.x, coord.y, coord.z, chunk->hash());
    }

    std::ranges::sort(snapshot);
    return snapshot;
  }

  terrain::BrushOp brush(const terrain::BrushOp::Kind kind, const glm::vec3 center, const float radius,
                         const float strength = 1.0f)
  {
    terrain::BrushOp op;
    op.kind = kind;
    op.center = center;
    op.radius = radius;
    op.strength = strength;
    op.material = 3;
    return op;
  }

  struct TerrainEnv
  {
    terrain::TerrainStore store;
    std::shared_ptr<terrain::TerrainVolume> volume;

    TerrainEnv()
    {
      terrain::TerrainDescription description;
      description.uuid = terrainId;
      description.voxelSize = 1.0f;
      volume = store.create(description);
    }
  };

  // An editor view, a server that mirrors it through the wire path, and the history that records the editor's
  // strokes.
  struct World
  {
    fixtures::Scene scene;
    TerrainEnv editor;
    TerrainEnv server;
    edits::EditHistory history;

    // Applies the ops the way a stroke does (the before pointers are taken ahead of each commit) and returns the
    // command for the result without recording it.
    std::optional<edits::EditCommand> sculptCommand(const std::initializer_list<terrain::BrushOp> ops)
    {
      BeforeList before;
      std::unordered_set<terrain::ChunkCoord> seen;

      for (const auto& op : ops)
      {
        const auto chunkEdits = terrain::applyBrush(*editor.volume, op);

        for (const auto& [coord, chunk] : chunkEdits)
        {
          if (seen.insert(coord).second)
          {
            before.emplace_back(coord, editor.volume->chunk(coord));
          }
        }

        terrain::commit(*editor.volume, chunkEdits);
      }

      return edits::commandForTerrainStroke(terrainId, before, *editor.volume);
    }

    // Sculpts, records the stroke and mirrors it to the server.
    bool sculpt(const std::initializer_list<terrain::BrushOp> ops)
    {
      auto command = sculptCommand(ops);

      if (!command)
      {
        return false;
      }

      mirror(command->buildRedoTerrainChunks());
      history.record(std::move(*command));
      return true;
    }

    void mirror(const edits::TerrainSetChunks& payload)
    {
      for (const auto& message : edits::packSetChunksEdits(payload, 1))
      {
        const auto unpacked = terrain::unpackTerrainEdit(message);
        ASSERT_TRUE(unpacked.has_value());
        EXPECT_EQ(terrain::applyTerrainEdit(server.store, *unpacked).refusal, terrain::TerrainEditRefusal::none);
      }
    }

    [[nodiscard]] edits::HistoryOutcome undo()
    {
      return history.undo(*scene.objectManager, nullptr, nullptr, &editor.store);
    }

    [[nodiscard]] edits::HistoryOutcome redo()
    {
      return history.redo(*scene.objectManager, nullptr, nullptr, &editor.store);
    }

    // Applies an outcome's payload to the editor volume and sends it to the server.
    void apply(const edits::HistoryOutcome& outcome)
    {
      ASSERT_TRUE(outcome.ok());
      ASSERT_TRUE(outcome.terrainPayload.has_value());
      EXPECT_FALSE(outcome.jsonPayload.has_value());
      EXPECT_FALSE(outcome.messagePayload.has_value());

      mirror(*outcome.terrainPayload);
      edits::predictSetChunks(*editor.volume, *outcome.terrainPayload);
    }
  };

  // Stroke i of the twenty-stroke sequence: three strokes per group, groups marching along x so some strokes
  // overlap the chunks of earlier ones, some reach fresh chunks out of air and one crosses a chunk boundary.
  bool sculptNth(World& world, const int index)
  {
    const int group = index / 3;
    const glm::vec3 base(8.0f + 9.0f * static_cast<float>(group), 14.0f, 16.0f + 6.0f * static_cast<float>(group % 2));

    switch (index % 3)
    {
      case 0:
        return world.sculpt({ brush(terrain::BrushOp::Kind::add, base, 5.0f) });
      case 1:
        return world.sculpt({ brush(terrain::BrushOp::Kind::add, base + glm::vec3(4.0f, 3.0f, 0.0f), 5.0f),
                              brush(terrain::BrushOp::Kind::add, base + glm::vec3(-2.0f, -2.0f, 1.0f), 4.0f) });
      default:
        return world.sculpt({ brush(terrain::BrushOp::Kind::subtract, base + glm::vec3(2.0f, 1.0f, 0.0f), 3.0f) });
    }
  }

  void expectCoherentWorld(const World& world, const Snapshot& expected)
  {
    EXPECT_EQ(snapshotOf(*world.editor.volume), expected);
    EXPECT_EQ(snapshotOf(*world.server.volume), expected);
  }

  ChunkPtr foreignChunk()
  {
    return terrain::TerrainChunk::uniform(0, 5);
  }

  terrain::ChunkCoord firstRecordedCoord(const edits::EditCommand& command)
  {
    return command.terrainStrokeChunks()->front().coord;
  }

  edits::TerrainSetChunks uniformChunks(const int count)
  {
    edits::TerrainSetChunks payload;
    payload.terrain = terrainId;

    for (int i = 0; i < count; ++i)
    {
      payload.chunks.push_back({ { i, 0, 0 }, terrain::TerrainChunk::uniform(0, static_cast<uint8_t>(1 + i % 200)) });
    }

    return payload;
  }
}

TEST(TerrainEditHistory, TwentyStrokesUndoAndRedoThroughTheWireBackToEveryState)
{
  World world;
  std::vector<Snapshot> states{ snapshotOf(*world.editor.volume) };
  constexpr int strokes = 20;

  for (int i = 0; i < strokes; ++i)
  {
    ASSERT_TRUE(sculptNth(world, i)) << "stroke " << i;
    states.push_back(snapshotOf(*world.editor.volume));
    ASSERT_NE(states[i], states[i + 1]) << "stroke " << i;
  }

  expectCoherentWorld(world, states[strokes]);

  for (int step = strokes; step >= 1; --step)
  {
    const auto outcome = world.undo();
    ASSERT_TRUE(outcome.ok()) << "undo " << step;
    world.apply(outcome);
    expectCoherentWorld(world, states[step - 1]);
  }

  EXPECT_FALSE(world.history.canUndo());
  EXPECT_EQ(world.history.redoDepth(), static_cast<std::size_t>(strokes));

  for (int step = 1; step <= strokes; ++step)
  {
    const auto outcome = world.redo();
    ASSERT_TRUE(outcome.ok()) << "redo " << step;
    world.apply(outcome);
    expectCoherentWorld(world, states[step]);
  }

  EXPECT_FALSE(world.history.canRedo());
}

TEST(TerrainEditHistory, AStrokeThatErasesAChunkToAirUndoesAndRedoes)
{
  World world;
  ASSERT_TRUE(world.sculpt({ brush(terrain::BrushOp::Kind::add, { 16.0f, 16.0f, 16.0f }, 6.0f) }));
  const Snapshot withBlob = snapshotOf(*world.editor.volume);
  ASSERT_FALSE(withBlob.empty());

  // A brush leaves painted material behind in an emptied chunk, so the stroke is committed as the air an edit
  // pipeline emits for a chunk that holds no ground.
  const BeforeList before{ { { 0, 0, 0 }, world.editor.volume->chunk({ 0, 0, 0 }) } };
  terrain::commit(*world.editor.volume, { { { 0, 0, 0 }, nullptr } });
  auto command = edits::commandForTerrainStroke(terrainId, before, *world.editor.volume);
  ASSERT_TRUE(command.has_value());
  world.mirror(command->buildRedoTerrainChunks());
  world.history.record(std::move(*command));

  const Snapshot erased = snapshotOf(*world.editor.volume);
  EXPECT_TRUE(erased.empty());
  EXPECT_FALSE(world.editor.volume->chunk({ 0, 0, 0 }));

  world.apply(world.undo());
  expectCoherentWorld(world, withBlob);
  EXPECT_TRUE(world.editor.volume->chunk({ 0, 0, 0 }));

  world.apply(world.redo());
  expectCoherentWorld(world, erased);
  EXPECT_FALSE(world.server.volume->chunk({ 0, 0, 0 }));
}

TEST(TerrainEditHistory, UndoIsRefusedWhenAnotherEditorChangedATouchedChunk)
{
  World world;
  for (int i = 0; i < 3; ++i)
  {
    ASSERT_TRUE(sculptNth(world, i));
  }

  const Snapshot beforeUndo = snapshotOf(*world.editor.volume);
  // Every stroke of the first group lands in chunk 0.
  world.editor.volume->setChunk({ 0, 0, 0 }, foreignChunk());
  const Snapshot drifted = snapshotOf(*world.editor.volume);
  ASSERT_NE(beforeUndo, drifted);

  const auto outcome = world.undo();
  EXPECT_EQ(outcome.result, edits::HistoryResult::targetChanged);
  ASSERT_TRUE(outcome.conflict.has_value());
  EXPECT_EQ(*outcome.conflict, terrainId);
  EXPECT_FALSE(outcome.terrainPayload.has_value());
  EXPECT_FALSE(world.history.canUndo());
  EXPECT_EQ(snapshotOf(*world.editor.volume), drifted);

  World control;
  for (int i = 0; i < 3; ++i)
  {
    ASSERT_TRUE(sculptNth(control, i));
  }

  const auto controlOutcome = control.undo();
  EXPECT_TRUE(controlOutcome.ok());
  EXPECT_TRUE(control.history.canUndo());
}

TEST(TerrainEditHistory, RedoIsRefusedWhenAChunkDriftedFromTheRecordedBefore)
{
  const auto build = [](World& world) {
    for (int i = 0; i < 3; ++i)
    {
      ASSERT_TRUE(sculptNth(world, i));
    }

    world.apply(world.undo());
  };

  World world;
  build(world);
  ASSERT_TRUE(world.history.canRedo());

  world.editor.volume->setChunk({ 0, 0, 0 }, foreignChunk());

  const auto outcome = world.redo();
  EXPECT_EQ(outcome.result, edits::HistoryResult::targetChanged);
  ASSERT_TRUE(outcome.conflict.has_value());
  EXPECT_EQ(*outcome.conflict, terrainId);
  EXPECT_FALSE(world.history.canRedo());

  World control;
  build(control);
  const auto controlOutcome = control.redo();
  EXPECT_TRUE(controlOutcome.ok());
  EXPECT_TRUE(controlOutcome.terrainPayload.has_value());
}

TEST(TerrainEditHistory, AMissingTerrainRefusesUndoAndRedoNamingIt)
{
  World nullStore;
  ASSERT_TRUE(sculptNth(nullStore, 0));
  const auto noStore = nullStore.history.undo(*nullStore.scene.objectManager);
  EXPECT_EQ(noStore.result, edits::HistoryResult::targetMissing);
  ASSERT_TRUE(noStore.conflict.has_value());
  EXPECT_EQ(*noStore.conflict, terrainId);

  World removed;
  ASSERT_TRUE(sculptNth(removed, 0));
  ASSERT_TRUE(removed.editor.store.remove(terrainId));
  const auto gone = removed.undo();
  EXPECT_EQ(gone.result, edits::HistoryResult::targetMissing);
  ASSERT_TRUE(gone.conflict.has_value());
  EXPECT_EQ(*gone.conflict, terrainId);

  World redoRemoved;
  ASSERT_TRUE(sculptNth(redoRemoved, 0));
  redoRemoved.apply(redoRemoved.undo());
  ASSERT_TRUE(redoRemoved.editor.store.remove(terrainId));
  const auto redoGone = redoRemoved.redo();
  EXPECT_EQ(redoGone.result, edits::HistoryResult::targetMissing);
  ASSERT_TRUE(redoGone.conflict.has_value());
  EXPECT_EQ(*redoGone.conflict, terrainId);

  World control;
  ASSERT_TRUE(sculptNth(control, 0));
  EXPECT_TRUE(control.undo().ok());
}

TEST(TerrainEditHistory, AStrokeThatChangedNothingRecordsNothing)
{
  World world;
  ASSERT_TRUE(world.sculpt({ brush(terrain::BrushOp::Kind::add, { 16.0f, 16.0f, 16.0f }, 6.0f) }));

  BeforeList unchanged;
  for (const auto& [coord, chunk] : world.editor.volume->chunks())
  {
    unchanged.emplace_back(coord, chunk);
  }
  ASSERT_FALSE(unchanged.empty());
  EXPECT_FALSE(edits::commandForTerrainStroke(terrainId, unchanged, *world.editor.volume).has_value());

  const BeforeList none;
  EXPECT_FALSE(edits::commandForTerrainStroke(terrainId, none, *world.editor.volume).has_value());

  EXPECT_FALSE(world.sculptCommand({ brush(terrain::BrushOp::Kind::add, { 16.0f, 16.0f, 16.0f }, 6.0f, 0.0f) })
                 .has_value());

  EXPECT_TRUE(world.sculptCommand({ brush(terrain::BrushOp::Kind::subtract, { 16.0f, 16.0f, 16.0f }, 3.0f) })
                .has_value());
}

TEST(TerrainEditHistory, TheCommandKeepsOnlyChunksThatChangedInTheirInputOrder)
{
  World world;
  ASSERT_TRUE(world.sculpt({ brush(terrain::BrushOp::Kind::add, { 31.0f, 16.0f, 16.0f }, 5.0f) }));
  ASSERT_TRUE(world.editor.volume->chunk({ 0, 0, 0 }));
  ASSERT_TRUE(world.editor.volume->chunk({ 1, 0, 0 }));

  const ChunkPtr zero = world.editor.volume->chunk({ 0, 0, 0 });
  const ChunkPtr one = world.editor.volume->chunk({ 1, 0, 0 });
  const ChunkPtr changedZero = foreignChunk();
  world.editor.volume->setChunk({ 0, 0, 0 }, changedZero);

  const BeforeList before{ { { 9, 9, 9 }, nullptr }, { { 1, 0, 0 }, one }, { { 0, 0, 0 }, zero } };
  const auto command = edits::commandForTerrainStroke(terrainId, before, *world.editor.volume);
  ASSERT_TRUE(command.has_value());

  const auto* chunks = command->terrainStrokeChunks();
  ASSERT_TRUE(chunks != nullptr);
  ASSERT_EQ(chunks->size(), 1u);
  EXPECT_EQ(chunks->front().coord, (terrain::ChunkCoord{ 0, 0, 0 }));
  EXPECT_EQ(chunks->front().before, zero);
  EXPECT_EQ(chunks->front().after, changedZero);

  const BeforeList both{ { { 1, 0, 0 }, nullptr }, { { 0, 0, 0 }, zero } };
  const auto order = edits::commandForTerrainStroke(terrainId, both, *world.editor.volume);
  ASSERT_TRUE(order.has_value());
  ASSERT_EQ(order->terrainStrokeChunks()->size(), 2u);
  EXPECT_EQ(order->terrainStrokeChunks()->front().coord, (terrain::ChunkCoord{ 1, 0, 0 }));
  EXPECT_EQ(order->terrainStrokeChunks()->back().coord, (terrain::ChunkCoord{ 0, 0, 0 }));
}

TEST(TerrainEditHistory, TheCommandDescribesItselfAndIsNotGrouped)
{
  World world;
  auto first = world.sculptCommand({ brush(terrain::BrushOp::Kind::add, { 16.0f, 16.0f, 16.0f }, 5.0f) });
  ASSERT_TRUE(first.has_value());

  EXPECT_EQ(first->kind(), edits::CommandKind::terrainStroke);
  EXPECT_EQ(first->payloadForm(), edits::PayloadForm::terrainChunks);
  EXPECT_TRUE(first->isReversible());
  EXPECT_FALSE(first->targetsSceneScripts());
  EXPECT_EQ(first->primaryUUID(), terrainId);
  EXPECT_THROW((void)first->buildUndoJSON(*world.scene.objectManager), std::logic_error);
  EXPECT_THROW((void)first->buildRedoJSON(*world.scene.objectManager), std::logic_error);
  EXPECT_THROW((void)first->buildUndoMessage(*world.scene.objectManager), std::logic_error);
  EXPECT_THROW((void)first->buildRedoMessage(*world.scene.objectManager), std::logic_error);

  std::vector<edits::EditCommand> pair;
  pair.push_back(*first);
  pair.push_back(*first);
  EXPECT_THROW(world.history.recordBatch(std::move(pair)), std::invalid_argument);
  EXPECT_FALSE(world.history.canUndo());

  std::vector<edits::EditCommand> single;
  single.push_back(*first);
  world.history.recordBatch(std::move(single));
  ASSERT_TRUE(world.history.canUndo());

  const auto undoLabel = world.history.nextUndoLabel(*world.scene.objectManager);
  ASSERT_TRUE(undoLabel.has_value());
  EXPECT_EQ(*undoLabel, "Sculpt Terrain");
  EXPECT_FALSE(world.history.nextRedoLabel(*world.scene.objectManager).has_value());

  world.apply(world.undo());
  const auto redoLabel = world.history.nextRedoLabel(*world.scene.objectManager);
  ASSERT_TRUE(redoLabel.has_value());
  EXPECT_EQ(*redoLabel, "Sculpt Terrain");
}

TEST(TerrainEditHistory, UndoAndRedoPayloadsCarryTheBeforeAndAfterChunks)
{
  World world;
  ASSERT_TRUE(world.sculpt({ brush(terrain::BrushOp::Kind::add, { 31.0f, 16.0f, 16.0f }, 5.0f) }));
  const ChunkPtr after = world.editor.volume->chunk({ 0, 0, 0 });
  ASSERT_TRUE(after);

  const auto undo = world.undo();
  ASSERT_TRUE(undo.ok());
  ASSERT_TRUE(undo.terrainPayload.has_value());
  EXPECT_EQ(undo.terrainPayload->terrain, terrainId);
  ASSERT_FALSE(undo.terrainPayload->chunks.empty());
  for (const auto& entry : undo.terrainPayload->chunks)
  {
    EXPECT_FALSE(entry.chunk);
  }

  world.apply(undo);
  const auto redo = world.redo();
  ASSERT_TRUE(redo.ok());
  ASSERT_TRUE(redo.terrainPayload.has_value());

  const auto found = std::ranges::find_if(redo.terrainPayload->chunks, [](const terrain::SetChunkEntry& entry) {
    return entry.coord == terrain::ChunkCoord{ 0, 0, 0 };
  });
  ASSERT_NE(found, redo.terrainPayload->chunks.end());
  EXPECT_EQ(found->chunk, after);
}

TEST(TerrainEditHistory, LargeSetChunksSplitIntoMessagesWithinBothLimits)
{
  // The worst entry is 24 header bytes (3 x int32, hash, size) plus maxEncodedChunkBytes: 32768 samples / 128 per run
  // = 256 runs, each 1 + 128 * 2 distance bytes and 1 + 128 material bytes = 386, so 1 + 256 * 386 = 98817 and
  // 98841 per entry. The 32 MiB budget (33554432) holds floor(33554432 / 98841) = 339 of them, below the 1024-entry
  // cap, so the byte limit binds; the entry cap only guards against a smaller maxEncodedChunkBytes.
  constexpr std::size_t perMessage = 339;
  static_assert(perMessage < terrain::maxTerrainChunksEntries);
  constexpr int total = 3 * perMessage + 1;

  constexpr std::size_t worstEntry = terrain::setChunkEntryHeaderBytes + terrain::maxEncodedChunkBytes;
  const std::size_t expectedCounts[] = { perMessage, perMessage, perMessage, 1 };

  const auto payload = uniformChunks(total);
  const auto messages = edits::packSetChunksEdits(payload, 42);
  ASSERT_EQ(messages.size(), 4u);

  std::size_t next = 0;
  for (std::size_t m = 0; m < messages.size(); ++m)
  {
    const auto unpacked = terrain::unpackTerrainEdit(messages[m]);
    ASSERT_TRUE(unpacked.has_value());
    EXPECT_EQ(unpacked->kind, terrain::TerrainEditKind::setChunks);
    EXPECT_EQ(unpacked->terrain, terrainId);
    EXPECT_EQ(unpacked->strokeId, 42u);
    EXPECT_EQ(unpacked->chunks.size(), expectedCounts[m]);
    EXPECT_LE(unpacked->chunks.size() * worstEntry, edits::maxSetChunksMessageBytes);
    EXPECT_LE(messages[m].size(), 64u * 1024u * 1024u);

    for (const auto& entry : unpacked->chunks)
    {
      ASSERT_LT(next, payload.chunks.size());
      EXPECT_EQ(entry.coord, payload.chunks[next].coord);
      EXPECT_EQ(entry.chunk->hash(), payload.chunks[next].chunk->hash());
      ++next;
    }
  }

  EXPECT_EQ(next, static_cast<std::size_t>(total));

  EXPECT_EQ(edits::packSetChunksEdits(uniformChunks(10)).size(), 1u);
  EXPECT_TRUE(edits::packSetChunksEdits(uniformChunks(0)).empty());
}

TEST(TerrainEditHistory, AirIsTheSameAsNullWhenComparingChunks)
{
  const auto air = terrain::TerrainChunk::uniform(terrain::airDistance, 0);
  ASSERT_TRUE(air);
  ASSERT_TRUE(air->isAir());
  const auto ground = foreignChunk();
  ASSERT_FALSE(ground->isAir());

  EXPECT_FALSE(edits::terrainChunksDiffer(air, nullptr));
  EXPECT_FALSE(edits::terrainChunksDiffer(nullptr, air));
  EXPECT_TRUE(edits::terrainChunksDiffer(ground, nullptr));
  EXPECT_TRUE(edits::terrainChunksDiffer(air, ground));

  constexpr terrain::ChunkCoord coord{ 50, 0, 0 };

  {
    World world;
    world.history.record(edits::EditCommand::terrainStroke(terrainId, { { coord, ground, air } }));
    const auto outcome = world.undo();
    EXPECT_TRUE(outcome.ok());
    ASSERT_TRUE(outcome.terrainPayload.has_value());
    edits::predictSetChunks(*world.editor.volume, *outcome.terrainPayload);
    EXPECT_EQ(world.editor.volume->chunk(coord)->hash(), ground->hash());
    EXPECT_TRUE(world.redo().ok());
  }

  {
    World world;
    world.editor.volume->setChunk(coord, ground);
    const BeforeList before{ { coord, air } };
    world.editor.volume->setChunk(coord, nullptr);
    EXPECT_FALSE(edits::commandForTerrainStroke(terrainId, before, *world.editor.volume).has_value());
    world.editor.volume->setChunk(coord, ground);
    EXPECT_TRUE(edits::commandForTerrainStroke(terrainId, before, *world.editor.volume).has_value());
  }
}

TEST(TerrainEditHistory, TheFactoryRefusesWhatTheServerWouldRefuse)
{
  const auto ground = foreignChunk();
  constexpr terrain::ChunkCoord coord{ 1, 2, 3 };

  EXPECT_NO_THROW((void)edits::EditCommand::terrainStroke(terrainId, { { coord, nullptr, ground } }));
  EXPECT_NO_THROW((void)edits::EditCommand::terrainStroke(
    terrainId, { { coord, nullptr, ground }, { { 1, 2, 4 }, nullptr, ground } }));
  EXPECT_NO_THROW((void)edits::EditCommand::terrainStroke(
    terrainId, { { { terrain::maxChunkCoord, -terrain::maxChunkCoord, 0 }, nullptr, ground } }));

  EXPECT_THROW((void)edits::EditCommand::terrainStroke(terrainId, {}), std::invalid_argument);
  EXPECT_THROW((void)edits::EditCommand::terrainStroke(
                 terrainId, { { coord, nullptr, ground }, { coord, ground, nullptr } }),
               std::invalid_argument);
  EXPECT_THROW((void)edits::EditCommand::terrainStroke(
                 terrainId, { { { terrain::maxChunkCoord + 1, 0, 0 }, nullptr, ground } }),
               std::invalid_argument);
  EXPECT_THROW((void)edits::EditCommand::terrainStroke(
                 terrainId, { { { 0, -terrain::maxChunkCoord - 1, 0 }, nullptr, ground } }),
               std::invalid_argument);
}

TEST(TerrainEditHistory, ARepeatedCoordinateInTheBeforeListKeepsTheFirstEntry)
{
  World world;
  constexpr terrain::ChunkCoord coord{ 7, 0, 0 };
  const auto ground = foreignChunk();
  world.editor.volume->setChunk(coord, ground);

  const BeforeList before{ { coord, nullptr }, { coord, ground } };
  const auto command = edits::commandForTerrainStroke(terrainId, before, *world.editor.volume);
  ASSERT_TRUE(command.has_value());
  ASSERT_EQ(command->terrainStrokeChunks()->size(), 1u);
  EXPECT_EQ(command->terrainStrokeChunks()->front().before, nullptr);
  EXPECT_EQ(command->terrainStrokeChunks()->front().after, ground);
}
