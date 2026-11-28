#ifndef TERRAINSETCHUNKS_H
#define TERRAINSETCHUNKS_H

#include "terrain/TerrainChunk.h"
#include "terrain/TerrainEdit.h"
#include "terrain/TerrainMaterialRemap.h"
#include "terrain/TerrainStore.h"
#include <Protocol.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <uuid.h>
#include <vector>

namespace edits {

// The palette half of an undo or redo of a material removal or move (EditCommand::terrainPaletteRemap).
struct TerrainPaletteStep {
  // The palette the terrain holds once the step applies.
  std::vector<terrain::TerrainMaterial> palette;
  // Redo only: the remap the server applies in place of the chunks, built for the palette hashing to fromPaletteHash.
  std::optional<terrain::MaterialRemap> remap;
  uint64_t fromPaletteHash = 0;
};

// A terrain setChunks the editor sends to the server and applies to its own volume as a prediction.
struct TerrainSetChunks {
  uuids::uuid terrain;
  std::vector<terrain::SetChunkEntry> chunks;
  std::optional<TerrainPaletteStep> palette;
};

// The transport refuses a message over 64 MiB and a dense chunk can encode to maxEncodedChunkBytes, so a message
// stays well inside that.
inline constexpr std::size_t maxSetChunksMessageBytes = 32 * 1024 * 1024;

// Splits the payload, in order, into terrainEdit messages of at most maxTerrainChunksEntries entries and at most
// maxSetChunksMessageBytes, counting every entry at its worst case. Empty for an empty payload.
[[nodiscard]] std::vector<net::Message> packSetChunksEdits(const TerrainSetChunks& payload, uint64_t strokeId = 0);

// Sets each entry on the volume the way the server will, without touching a revision.
void predictSetChunks(terrain::TerrainVolume& volume, const TerrainSetChunks& payload);

// The messages an undo or redo payload sends, in order. Without a palette step: packSetChunksEdits. With one: the
// remapMaterials edit alone when it carries a remap (redo), else the setChunks messages followed by the addAsset that
// replaces the palette (undo). Empty, sending nothing, for a palette step whose terrain the view does not hold.
[[nodiscard]] std::vector<net::Message> packTerrainStep(const TerrainSetChunks& payload,
                                                        const terrain::TerrainStore& view, uint64_t strokeId = 0);

// Applies the payload to the view the way the server will: the remap (commitRemap) or the chunks, then the palette.
// False, with nothing applied, when the view does not hold the terrain or the palette is invalid.
bool predictTerrainStep(terrain::TerrainStore& view, const TerrainSetChunks& payload);

// Whether the two differ as the volume holds them: null and air are the same, and two held chunks differ by hash.
[[nodiscard]] bool terrainChunksDiffer(const std::shared_ptr<const terrain::TerrainChunk>& first,
                                       const std::shared_ptr<const terrain::TerrainChunk>& second);

}

#endif //TERRAINSETCHUNKS_H
