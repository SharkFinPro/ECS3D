#ifndef TERRAINEDITECHOFIXTURES_H
#define TERRAINEDITECHOFIXTURES_H

#include "scenes/SceneManager.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainEditEcho.h"
#include "terrain/TerrainReplication.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainViewSync.h"

#include <Protocol.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

#include "TerrainStreamerFixtures.h"

// Shared by the terrain edit echo suites.
namespace terrainEchoFixtures
{
  using namespace terrainStreamerFixtures;

  inline const std::vector<int32_t> bothConnections{ 1, 2 };

  // Distinct content per index, never air.
  inline std::shared_ptr<const terrain::TerrainChunk> variant(const int index)
  {
    return terrain::TerrainChunk::uniform(static_cast<int16_t>(-100 - index), 1);
  }

  inline void makeTerrain(terrain::TerrainStore& store)
  {
    ASSERT_NE(store.create({ .uuid = terrainA }), nullptr);
    // The server's first edit then lands above the revision 1 a view holds.
    ASSERT_EQ(store.nextRevision(terrainA), 1u);
  }

  // The server applying an edit: the new content under a fresh revision.
  inline void serverSet(terrain::TerrainStore& store, const ChunkCoord coord,
                 const std::shared_ptr<const terrain::TerrainChunk>& chunk)
  {
    ASSERT_TRUE(store.setChunk(terrainA, coord, chunk, store.nextRevision(terrainA)));
  }

  // A view holding content under revision 1, below anything the server hands out afterward.
  inline void viewHolds(terrain::TerrainStore& store, const ChunkCoord coord,
                 const std::shared_ptr<const terrain::TerrainChunk>& chunk)
  {
    ASSERT_NE(store.create({ .uuid = terrainA }), nullptr);
    ASSERT_TRUE(store.setChunk(terrainA, coord, chunk, 1));
  }

  inline void flush(terrain::TerrainEditEcho& echo, terrain::TerrainStore& store, Recorder& recorder,
             const std::vector<int32_t>& connections = bothConnections,
             const size_t budget = terrain::defaultStreamBudgetBytes)
  {
    echo.flush(store, connections, recorder.sender(), budget);
  }

  inline uint64_t heldHash(const terrain::TerrainStore& store, const ChunkCoord coord)
  {
    return store.find(terrainA)->chunk(coord)->hash();
  }
}

#endif //TERRAINEDITECHOFIXTURES_H
