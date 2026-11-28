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
  enum class TerrainEditRefusal : uint8_t
  {
    none,
    unknownTerrain,
    invalidOp,
    tooLarge,
    revisionsExhausted,
    paletteChanged
  };

  struct TerrainEditResult
  {
    TerrainEditRefusal refusal = TerrainEditRefusal::none;
    // The one revision every changed chunk was given; 0 when nothing changed.
    uint32_t revision = 0;
    // In the order applied; empty for a refusal or an edit that changed nothing.
    std::vector<ChunkCoord> changed;
    // A remapMaterials edit gave the terrain its new palette; the caller re-snapshots so every view receives it.
    bool paletteReplaced = false;
  };

  // The server side of a terrainEdit. Bounds the operation against the terrain's volume, applies it through
  // TerrainStore::setChunk with one fresh revision, and changes nothing when it refuses. A setChunks payload must
  // name each coordinate once, as unpackTerrainEdit guarantees; a null or air chunk means air whatever its material. A
  // remapMaterials edit touches every held chunk, so it is not bounded by maxEditChunks; it is refused with
  // paletteChanged unless the held palette hashes to its paletteHash, and its chunks and palette apply together.
  [[nodiscard]] TerrainEditResult applyTerrainEdit(TerrainStore& store, const TerrainEditPayload& edit);

  // The repair for a remapMaterials edit refused with paletteChanged, whose sender already predicted it: the chunks the
  // remap would have changed on the server's volume, in z, y, x order, each given one fresh revision so a full send
  // replaces the sender's prediction held under an older one. The caller sends them in full to the sender alone;
  // chunks the sender predicted from a view that differs from the server are left to the echo's ordinary repair.
  // Empty, stamping nothing, for another kind, an unknown terrain, an invalid remap or an exhausted counter.
  [[nodiscard]] std::vector<ChunkCoord> restampRefusedRemap(TerrainStore& store, const TerrainEditPayload& edit);

  // Lower-case phrase for a log line, e.g. "the operation is too large".
  [[nodiscard]] std::string_view refusalText(TerrainEditRefusal refusal);
}

#endif //TERRAINEDITAPPLY_H
