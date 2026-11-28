#ifndef TERRAINSTROKETYPES_H
#define TERRAINSTROKETYPES_H

#include "terrain/TerrainMaterialRemap.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainTypes.h"
#include <memory>
#include <vector>

namespace terrain {
  class TerrainChunk;
}

namespace edits {

// One chunk a sculpt stroke changed. Chunks are immutable, so comparing the pointers compares the content a
// command recorded; null means air.
struct TerrainStrokeChunk {
  terrain::ChunkCoord coord;
  std::shared_ptr<const terrain::TerrainChunk> before;
  std::shared_ptr<const terrain::TerrainChunk> after;

  friend bool operator==(const TerrainStrokeChunk&, const TerrainStrokeChunk&) = default;
};

// The palette half of a material removal or move, recorded with the chunks its remap changed so one entry reverses
// both: the palette before and after, and the remap that redo sends again.
struct TerrainPaletteRemap {
  std::vector<terrain::TerrainMaterial> before;
  std::vector<terrain::TerrainMaterial> after;
  terrain::MaterialRemap remap = terrain::identityRemap();

  friend bool operator==(const TerrainPaletteRemap&, const TerrainPaletteRemap&) = default;
};

}

#endif //TERRAINSTROKETYPES_H
