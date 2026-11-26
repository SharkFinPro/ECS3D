#ifndef TERRAINEDITAPPLY_H
#define TERRAINEDITAPPLY_H

#include "TerrainEdit.h"
#include "TerrainStore.h"
#include "TerrainTypes.h"
#include <cstdint>
#include <string_view>
#include <vector>

namespace terrain
{
  enum class TerrainEditRefusal : uint8_t { none, unknownTerrain, invalidOp, tooLarge, revisionsExhausted };

  struct TerrainEditResult
  {
    TerrainEditRefusal refusal = TerrainEditRefusal::none;
    // The one revision every changed chunk was given; 0 when nothing changed.
    uint32_t revision = 0;
    // In the order applied; empty for a refusal or an edit that changed nothing.
    std::vector<ChunkCoord> changed;
  };

  // The server side of a terrainEdit. Bounds the operation against the terrain's volume, applies it through
  // TerrainStore::setChunk with one fresh revision, and changes nothing when it refuses. A setChunks payload must
  // name each coordinate once, as unpackTerrainEdit guarantees; a null or air chunk means air whatever its material.
  [[nodiscard]] TerrainEditResult applyTerrainEdit(TerrainStore& store, const TerrainEditPayload& edit);

  // Lower-case phrase for a log line, e.g. "the operation is too large".
  [[nodiscard]] std::string_view refusalText(TerrainEditRefusal refusal);
}

#endif //TERRAINEDITAPPLY_H
