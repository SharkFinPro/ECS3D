#ifndef TERRAINPLAYSNAPSHOT_H
#define TERRAINPLAYSNAPSHOT_H

#include "TerrainStore.h"
#include "TerrainTypes.h"
#include "TerrainVolume.h"
#include <map>
#include <utility>
#include <uuid.h>
#include <vector>

namespace terrain
{
  // The chunk maps of every terrain at Start, so Stop can put back what play changed. Holds pointers only: chunks are
  // immutable, so nothing is copied. Not thread safe.
  class TerrainPlaySnapshot
  {
  public:
    // Replaces any earlier capture.
    void capture(const TerrainStore& store);

    void clear();

    [[nodiscard]] bool empty() const;

    // Puts back every captured terrain the store still holds: each coordinate whose chunk pointer differs from the
    // capture (changed, added or erased since) is set to the captured chunk, or erased, with a fresh nextRevision.
    // Returns the restored coordinates per terrain in uuid order, each list sorted; terrains created after the capture
    // are left alone, and so is one whose volume now has a different voxel size than at capture (its chunks mean
    // something else). Clears the snapshot.
    std::vector<std::pair<uuids::uuid, std::vector<ChunkCoord>>> restore(TerrainStore& store);

  private:
    struct Captured
    {
      float voxelSize = 0.0f;
      TerrainVolume::ChunkMap chunks;
    };

    std::map<uuids::uuid, Captured> m_terrains;
  };
}

#endif //TERRAINPLAYSNAPSHOT_H