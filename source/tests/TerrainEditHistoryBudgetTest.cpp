#include <gtest/gtest.h>

#include "TestScene.h"
#include "edits/EditCommand.h"
#include "edits/EditHistory.h"
#include "edits/TerrainSetChunks.h"
#include "objects/ObjectManager.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <utility>
#include <uuid.h>
#include <vector>

namespace {
  const auto terrainId = uuids::uuid::from_string("22222222-2222-2222-2222-222222222222").value();
  const terrain::ChunkCoord coord{ 0, 0, 0 };

  using ChunkPtr = std::shared_ptr<const terrain::TerrainChunk>;

  // Not uniform, so it holds the full sample arrays.
  ChunkPtr denseChunk(const int seed)
  {
    std::vector<int16_t> distances(static_cast<std::size_t>(terrain::samplesPerChunk), 0);
    distances[0] = static_cast<int16_t>(seed);
    std::vector<uint8_t> materials(static_cast<std::size_t>(terrain::samplesPerChunk), 1);
    return terrain::TerrainChunk::dense(std::move(distances), std::move(materials));
  }

  std::size_t strokeBytesOfDenseChunks()
  {
    return denseChunk(1)->byteSize() * 2;
  }

  edits::EditCommand strokeBetween(const ChunkPtr& before, const ChunkPtr& after)
  {
    return edits::EditCommand::terrainStroke(terrainId, { { coord, before, after } });
  }

  // Stroke i takes the live chunk from chunks[i] to chunks[i + 1].
  struct World
  {
    fixtures::Scene scene;
    terrain::TerrainStore store;
    std::shared_ptr<terrain::TerrainVolume> volume;
    std::vector<ChunkPtr> chunks;
    std::size_t strokeBytes = strokeBytesOfDenseChunks();
    edits::EditHistory history;

    // strokesInBudget 0 uses the default budget.
    explicit World(const std::size_t strokesInBudget = 0)
      : history(strokesInBudget == 0 ? edits::EditHistory::maxPinnedBytes : strokesInBudget * strokeBytesOfDenseChunks())
    {
      terrain::TerrainDescription description;
      description.uuid = terrainId;
      description.voxelSize = 1.0f;
      volume = store.create(description);

      for (int i = 0; i < 12; ++i)
      {
        chunks.push_back(denseChunk(i + 1));
      }

      volume->setChunk(coord, chunks.front());
    }

    [[nodiscard]] edits::EditCommand stroke(const int i) const
    {
      return strokeBetween(chunks[static_cast<std::size_t>(i)], chunks[static_cast<std::size_t>(i) + 1]);
    }

    void record(const int i)
    {
      history.record(stroke(i));
      volume->setChunk(coord, chunks[static_cast<std::size_t>(i) + 1]);
    }

    edits::HistoryOutcome undo()
    {
      const auto outcome = history.undo(*scene.objectManager, nullptr, nullptr, &store);
      if (outcome.terrainPayload)
      {
        edits::predictSetChunks(*volume, *outcome.terrainPayload);
      }

      return outcome;
    }

    edits::HistoryOutcome redo()
    {
      const auto outcome = history.redo(*scene.objectManager, nullptr, nullptr, &store);
      if (outcome.terrainPayload)
      {
        edits::predictSetChunks(*volume, *outcome.terrainPayload);
      }

      return outcome;
    }
  };

  // The position before the first undo and after each one, until nothing is left to undo.
  std::vector<std::uint64_t> undoAllPositions(World& world)
  {
    std::vector<std::uint64_t> positions{ world.history.position() };

    while (world.history.canUndo())
    {
      EXPECT_TRUE(world.undo().ok());
      positions.push_back(world.history.position());
    }

    return positions;
  }
}

TEST(TerrainEditHistoryBudget, PinnedBytesSumsEveryNonNullChunkAndIsZeroForOtherKinds)
{
  const auto a = denseChunk(1);
  const auto b = denseChunk(2);
  const auto uniform = terrain::TerrainChunk::uniform(0, 1);
  ASSERT_TRUE(a && b && uniform);
  ASSERT_NE(uniform->byteSize(), a->byteSize());

  const auto two = edits::EditCommand::terrainStroke(terrainId, { { coord, a, b }, { { 1, 0, 0 }, nullptr, uniform } });
  EXPECT_EQ(two.pinnedBytes(), a->byteSize() + b->byteSize() + uniform->byteSize());

  EXPECT_EQ(strokeBetween(a, a).pinnedBytes(), a->byteSize() * 2);

  const nlohmann::json blob = { { "type", "Transform" } };
  const auto edit = edits::EditCommand::componentEdit(terrainId, blob, blob);
  EXPECT_EQ(edit.pinnedBytes(), 0u);
}

TEST(TerrainEditHistoryBudget, RecordingPastTheBudgetKeepsTheNewestStrokes)
{
  World world(3);
  ASSERT_GT(world.strokeBytes, 0u);

  for (int i = 0; i < 5; ++i)
  {
    world.record(i);
    EXPECT_LE(world.history.pinnedBytes(), 3 * world.strokeBytes);
  }

  EXPECT_EQ(world.history.undoDepth(), 3u);
  EXPECT_EQ(world.history.pinnedBytes(), 3 * world.strokeBytes);

  // Undoing hands back the before chunk of the newest strokes, 4, 3 and 2, in that order.
  for (int i = 4; i >= 2; --i)
  {
    const auto outcome = world.undo();
    ASSERT_TRUE(outcome.ok());
    ASSERT_TRUE(outcome.terrainPayload.has_value());
    EXPECT_EQ(outcome.terrainPayload->chunks.front().chunk, world.chunks[static_cast<std::size_t>(i)]);
  }

  EXPECT_FALSE(world.history.canUndo());
}

TEST(TerrainEditHistoryBudget, TheDefaultBudgetKeepsEveryStroke)
{
  World world;

  for (int i = 0; i < 5; ++i)
  {
    world.record(i);
  }

  EXPECT_EQ(world.history.undoDepth(), 5u);
  EXPECT_EQ(world.history.pinnedBytes(), 5 * world.strokeBytes);
}

TEST(TerrainEditHistoryBudget, AStrokeLargerThanTheBudgetIsStillUndoable)
{
  World world;
  edits::EditHistory tiny(world.strokeBytes / 2);

  tiny.record(world.stroke(0));
  EXPECT_EQ(tiny.undoDepth(), 1u);
  EXPECT_TRUE(tiny.canUndo());
  EXPECT_EQ(tiny.pinnedBytes(), world.strokeBytes);

  tiny.record(world.stroke(1));
  EXPECT_EQ(tiny.undoDepth(), 1u);
  EXPECT_EQ(tiny.pinnedBytes(), world.strokeBytes);
}

TEST(TerrainEditHistoryBudget, ASavePointSurvivesTrimmingOlderEntriesAndIsLostWhenTrimmed)
{
  World world(3);

  world.record(0);
  world.record(1);
  const std::uint64_t savePoint = world.history.position();
  world.record(2);
  world.record(3);

  // Entry 0 was trimmed and the save point (entry 1) is still on the stack.
  ASSERT_EQ(world.history.undoDepth(), 3u);
  ASSERT_TRUE(world.undo().ok());
  ASSERT_TRUE(world.undo().ok());
  EXPECT_EQ(world.history.position(), savePoint);

  ASSERT_TRUE(world.redo().ok());
  ASSERT_TRUE(world.redo().ok());
  world.record(4);
  world.record(5);
  world.record(6);

  // Entries 1, 2 and 3 were trimmed, so no position the history can reach is the save point.
  EXPECT_EQ(world.history.undoDepth(), 3u);
  const auto positions = undoAllPositions(world);
  EXPECT_EQ(positions.size(), 4u);
  const std::set<std::uint64_t> reachable(positions.begin(), positions.end());
  EXPECT_EQ(reachable.count(savePoint), 0u);
}

TEST(TerrainEditHistoryBudget, TrimmingTheOldestEntryInvalidatesTheEmptyStackSavePoint)
{
  World trimmed(2);
  const std::uint64_t trimmedSavePoint = trimmed.history.position();
  for (int i = 0; i < 5; ++i)
  {
    trimmed.record(i);
  }

  ASSERT_EQ(trimmed.history.undoDepth(), 2u);
  undoAllPositions(trimmed);
  ASSERT_FALSE(trimmed.history.canUndo());
  EXPECT_NE(trimmed.history.position(), trimmedSavePoint);

  World control;
  const std::uint64_t controlSavePoint = control.history.position();
  for (int i = 0; i < 5; ++i)
  {
    control.record(i);
  }

  ASSERT_EQ(control.history.undoDepth(), 5u);
  undoAllPositions(control);
  ASSERT_FALSE(control.history.canUndo());
  EXPECT_EQ(control.history.position(), controlSavePoint);
}

TEST(TerrainEditHistoryBudget, TheTotalTracksEveryPathThatAddsOrRemovesEntries)
{
  World world;

  world.record(0);
  world.record(1);
  EXPECT_EQ(world.history.pinnedBytes(), 2 * world.strokeBytes);

  ASSERT_TRUE(world.undo().ok());
  EXPECT_EQ(world.history.pinnedBytes(), 2 * world.strokeBytes);
  ASSERT_TRUE(world.redo().ok());
  EXPECT_EQ(world.history.pinnedBytes(), 2 * world.strokeBytes);

  // A new record after an undo clears the redo stack's bytes.
  ASSERT_TRUE(world.undo().ok());
  ASSERT_EQ(world.history.redoDepth(), 1u);
  world.history.record(strokeBetween(world.chunks[1], world.chunks[5]));
  EXPECT_EQ(world.history.redoDepth(), 0u);
  EXPECT_EQ(world.history.pinnedBytes(), 2 * world.strokeBytes);

  world.history.clear();
  EXPECT_EQ(world.history.pinnedBytes(), 0u);
}

TEST(TerrainEditHistoryBudget, RefusedUndoAndRedoReleaseTheirBytes)
{
  World refusedUndo;
  refusedUndo.record(0);
  refusedUndo.record(1);
  refusedUndo.volume->setChunk(coord, denseChunk(9));
  EXPECT_EQ(refusedUndo.undo().result, edits::HistoryResult::targetChanged);
  EXPECT_EQ(refusedUndo.history.pinnedBytes(), 0u);

  World control;
  control.record(0);
  control.record(1);
  EXPECT_TRUE(control.undo().ok());
  EXPECT_EQ(control.history.pinnedBytes(), 2 * control.strokeBytes);

  World refusedRedo;
  refusedRedo.record(0);
  refusedRedo.record(1);
  ASSERT_TRUE(refusedRedo.undo().ok());
  ASSERT_TRUE(refusedRedo.undo().ok());
  refusedRedo.volume->setChunk(coord, denseChunk(9));
  EXPECT_EQ(refusedRedo.redo().result, edits::HistoryResult::targetChanged);
  EXPECT_EQ(refusedRedo.history.pinnedBytes(), 0u);
  EXPECT_EQ(refusedRedo.history.redoDepth(), 0u);
}

TEST(TerrainEditHistoryBudget, RejectedRequestsAndMovesKeepTheTotalExact)
{
  World world;
  world.record(0);
  world.record(1);

  ASSERT_TRUE(world.undo().ok());
  world.history.reportUndoRejected();
  EXPECT_EQ(world.history.pinnedBytes(), world.strokeBytes);

  world.history.record(world.stroke(0));
  ASSERT_TRUE(world.undo().ok());
  ASSERT_TRUE(world.redo().ok());
  world.history.reportRedoRejected();
  EXPECT_EQ(world.history.pinnedBytes(), world.strokeBytes);

  edits::EditHistory moved(std::move(world.history));
  EXPECT_EQ(moved.pinnedBytes(), world.strokeBytes);
  EXPECT_EQ(world.history.pinnedBytes(), 0u);
  EXPECT_FALSE(world.history.canUndo());

  edits::EditHistory assigned;
  assigned = std::move(moved);
  EXPECT_EQ(assigned.pinnedBytes(), world.strokeBytes);
  EXPECT_EQ(moved.pinnedBytes(), 0u);
}
