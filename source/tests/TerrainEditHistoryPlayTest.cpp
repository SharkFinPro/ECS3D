#include <gtest/gtest.h>

#include "TestScene.h"
#include "edits/EditCommand.h"
#include "edits/EditHistory.h"
#include "edits/PlaySessionHistory.h"
#include "edits/RecordTerrainEdits.h"
#include "edits/TerrainSetChunks.h"
#include "objects/ObjectManager.h"
#include "scenes/SceneManager.h"
#include "terrain/TerrainBrush.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainEdit.h"
#include "terrain/TerrainEditApply.h"
#include "terrain/TerrainOps.h"
#include "terrain/TerrainPlaySnapshot.h"
#include "terrain/TerrainReplication.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <glm/vec3.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <uuid.h>
#include <vector>

namespace {
  const auto terrainId = uuids::uuid::from_string("11111111-1111-1111-1111-111111111111").value();

  using Snapshot = std::vector<std::tuple<int32_t, int32_t, int32_t, uint64_t>>;
  using BeforeList = std::vector<std::pair<terrain::ChunkCoord, std::shared_ptr<const terrain::TerrainChunk>>>;

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

  terrain::BrushOp addBrush(const glm::vec3 center, const float radius)
  {
    terrain::BrushOp op;
    op.kind = terrain::BrushOp::Kind::add;
    op.center = center;
    op.radius = radius;
    op.strength = 1.0f;
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

  // The editor's view and the server it edits, with the history the editor records into.
  struct PlayWorld
  {
    fixtures::Scene scene;
    TerrainEnv editor;
    TerrainEnv server;
    edits::PlaySessionHistory history;
    terrain::TerrainPlaySnapshot serverSnapshot;

    PlayWorld()
    {
      history.observeStatus(SceneStatus::stopped);
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

    void sculpt(const terrain::BrushOp& op)
    {
      const auto chunkEdits = terrain::applyBrush(*editor.volume, op);
      BeforeList before;

      for (const auto& [coord, chunk] : chunkEdits)
      {
        before.emplace_back(coord, editor.volume->chunk(coord));
      }

      terrain::commit(*editor.volume, chunkEdits);

      auto command = edits::commandForTerrainStroke(terrainId, before, *editor.volume);
      ASSERT_TRUE(command.has_value());
      mirror(command->buildRedoTerrainChunks());
      history.current().record(std::move(*command));
    }

    // The restore the server does on Stop, sent to the view as full chunks the way the streamer requeues them.
    void stopOnServer()
    {
      const auto restored = serverSnapshot.restore(server.store);
      ASSERT_EQ(restored.terrains.size(), 1u);

      std::vector<terrain::ChunkEntry> entries;
      for (const terrain::ChunkCoord coord : restored.terrains.front().second)
      {
        entries.push_back(terrain::makeChunkEntry(coord, server.store.revision(terrainId, coord),
                                                  server.volume->chunk(coord)));
      }

      const auto payload = terrain::unpackTerrainChunks(terrain::packTerrainChunks(terrainId, entries));
      ASSERT_TRUE(payload.has_value());
      terrain::applyTerrainChunks(editor.store, *payload);
    }

    [[nodiscard]] edits::HistoryOutcome undoAuthored()
    {
      return history.current().undo(*scene.objectManager, nullptr, nullptr, &editor.store);
    }
  };
}

TEST(TerrainEditHistoryPlayTest, AStrokeDrawnWhilePlayingIsDiscardedAndTheAuthoredStrokesStayUndoable)
{
  PlayWorld world;
  world.sculpt(addBrush({ 8.0f, 14.0f, 16.0f }, 5.0f));
  world.sculpt(addBrush({ 17.0f, 14.0f, 22.0f }, 5.0f));

  const size_t authoredDepth = world.history.current().undoDepth();
  const uint64_t authoredPosition = world.history.current().position();
  const Snapshot startState = snapshotOf(*world.editor.volume);
  ASSERT_EQ(authoredDepth, 2u);
  ASSERT_FALSE(startState.empty());

  ASSERT_TRUE(world.history.requestStart());
  world.serverSnapshot.capture(world.server.store);
  EXPECT_EQ(world.history.current().undoDepth(), 0u);

  // One stroke over a chunk the authored ones made and one that reaches a chunk out of air.
  world.sculpt(addBrush({ 12.0f, 14.0f, 18.0f }, 6.0f));
  world.sculpt(addBrush({ 70.0f, 14.0f, 16.0f }, 5.0f));
  EXPECT_EQ(world.history.current().undoDepth(), 2u);
  EXPECT_EQ(world.history.authoredPosition(), authoredPosition);

  // The control: play changed the view, so what follows is the restore's doing.
  EXPECT_NE(snapshotOf(*world.editor.volume), startState);
  EXPECT_NE(snapshotOf(*world.server.volume), startState);

  ASSERT_TRUE(world.history.requestStop());
  world.stopOnServer();

  EXPECT_EQ(world.history.current().undoDepth(), authoredDepth);
  EXPECT_EQ(world.history.current().position(), authoredPosition);
  EXPECT_EQ(snapshotOf(*world.editor.volume), startState);
  EXPECT_EQ(snapshotOf(*world.server.volume), startState);

  const Snapshot initial = snapshotOf(*TerrainEnv().volume);
  EXPECT_TRUE(initial.empty());

  for (size_t step = 0; step < authoredDepth; ++step)
  {
    const auto outcome = world.undoAuthored();
    ASSERT_TRUE(outcome.ok());
    ASSERT_TRUE(outcome.terrainPayload.has_value());
    world.mirror(*outcome.terrainPayload);
    edits::predictSetChunks(*world.editor.volume, *outcome.terrainPayload);
  }

  EXPECT_EQ(snapshotOf(*world.editor.volume), initial);
  EXPECT_EQ(snapshotOf(*world.server.volume), initial);
  EXPECT_FALSE(world.history.current().canUndo());
}
