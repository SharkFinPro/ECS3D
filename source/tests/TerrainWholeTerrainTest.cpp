#include <gtest/gtest.h>

#include "TerrainPaletteEditor.h"
#include "scenes/SceneManager.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainReplication.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainViewSync.h"
#include "terrain/TerrainVolume.h"

#include <cstdint>
#include <memory>
#include <vector>
#include <uuid.h>

namespace {
  const auto terrainId = uuids::uuid::from_string("bcbcbcbc-bcbc-4bcb-8bcb-bcbcbcbcbcbc").value();
  const auto firstLoad = uuids::uuid::from_string("cdcdcdcd-cdcd-4cdc-8cdc-cdcdcdcdcdcd").value();
  const auto secondLoad = uuids::uuid::from_string("dededede-dede-4ded-8ded-dededededede").value();

  // A view of a three-chunk terrain, fed the way the join stream and the echo feed the editor.
  struct View
  {
    terrain::TerrainStore store;
    terrain::TerrainViewSync sync;
    SceneManager scenes;
    uint32_t revision = 1;

    View()
    {
      EXPECT_NE(store.create({ .uuid = terrainId, .voxelSize = 1.0f, .chunkCount = 3 }), nullptr);
      sync.observeSnapshot(store, firstLoad);
    }

    void receive(const int x, const std::shared_ptr<const terrain::TerrainChunk>& chunk)
    {
      const std::vector<terrain::ChunkEntry> entries{ terrain::makeChunkEntry({ x, 0, 0 }, revision++, chunk) };
      const auto outcome = sync.apply(store, scenes, terrain::packTerrainChunks(terrainId, entries));
      EXPECT_FALSE(outcome.malformed);
    }

    void streamAll()
    {
      for (int x = 0; x < 3; ++x)
      {
        receive(x, terrain::TerrainChunk::uniform(-10, 1));
      }
    }

    [[nodiscard]] bool whole()
    {
      return sync.hasWholeTerrain(store, terrainId);
    }

    // The context EditorApp::terrainPaletteContext builds from it.
    [[nodiscard]] bool remapAllowed()
    {
      return !TerrainPaletteEditor::remapRefusal({ true, true, true, whole(), false }).has_value();
    }
  };
}

TEST(TerrainWholeTerrain, ATerrainStillStreamingIsRefusedAndAllowedOnceItArrives)
{
  View view;
  view.receive(0, terrain::TerrainChunk::uniform(-10, 1));
  view.receive(1, terrain::TerrainChunk::uniform(-10, 1));
  EXPECT_FALSE(view.remapAllowed());

  view.receive(2, terrain::TerrainChunk::uniform(-10, 1));
  EXPECT_TRUE(view.remapAllowed());
}

TEST(TerrainWholeTerrain, CarvingChunksToAirAfterTheTerrainArrivedKeepsItAllowed)
{
  View view;
  view.streamAll();
  ASSERT_TRUE(view.remapAllowed());

  // The snapshot's chunkCount is not refreshed, so two chunks carved to air leave the view below it for good.
  view.receive(0, nullptr);
  view.receive(1, nullptr);
  ASSERT_EQ(view.store.find(terrainId)->chunks().size(), 1u);
  EXPECT_TRUE(view.remapAllowed());

  // Control: a view that never held the whole terrain, at the same chunk count, is refused.
  View partial;
  partial.receive(2, terrain::TerrainChunk::uniform(-10, 1));
  EXPECT_FALSE(partial.remapAllowed());
}

TEST(TerrainWholeTerrain, ANewLoadRefusesUntilTheTerrainArrivesAgain)
{
  View view;
  view.streamAll();
  ASSERT_TRUE(view.whole());

  // The same load id is not a new load.
  view.sync.observeSnapshot(view.store, firstLoad);
  EXPECT_TRUE(view.whole());

  ASSERT_TRUE(view.sync.observeSnapshot(view.store, secondLoad));
  EXPECT_FALSE(view.whole());

  view.streamAll();
  EXPECT_TRUE(view.whole());
}

TEST(TerrainWholeTerrain, AReplacedVolumeOrAResetRefusesUntilTheTerrainArrivesAgain)
{
  View view;
  view.streamAll();
  ASSERT_TRUE(view.whole());

  // A new voxel size gives the terrain a fresh volume, which the stream fills again.
  ASSERT_TRUE(view.store.applyDescription({ .uuid = terrainId, .voxelSize = 2.0f, .chunkCount = 3 }));
  EXPECT_FALSE(view.whole());
  view.streamAll();
  EXPECT_TRUE(view.whole());

  view.receive(0, nullptr);
  view.sync.reset();
  EXPECT_FALSE(view.whole());
}

TEST(TerrainWholeTerrain, AnEmptyTerrainOrOneTheViewFilledItselfIsWhole)
{
  View view;
  ASSERT_TRUE(view.store.applyDescription({ .uuid = terrainId, .voxelSize = 1.0f, .chunkCount = 0 }));
  EXPECT_TRUE(view.whole());

  // An editor that created the terrain holds its chunks before anything streams.
  View created;
  for (int x = 0; x < 3; ++x)
  {
    created.store.find(terrainId)->setChunk({ x, 0, 0 }, terrain::TerrainChunk::uniform(-10, 1));
  }
  EXPECT_TRUE(created.whole());
  created.store.find(terrainId)->setChunk({ 0, 0, 0 }, nullptr);
  EXPECT_TRUE(created.whole());

  EXPECT_FALSE(view.sync.hasWholeTerrain(view.store, firstLoad));
}
