#ifndef TERRAINSETCHUNKS_H
#define TERRAINSETCHUNKS_H

#include "terrain/TerrainChunk.h"
#include "terrain/TerrainEdit.h"
#include <Protocol.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <uuid.h>
#include <vector>

namespace terrain {
  class TerrainVolume;
}

namespace edits {

// A terrain setChunks the editor sends to the server and applies to its own volume as a prediction.
struct TerrainSetChunks {
  uuids::uuid terrain;
  std::vector<terrain::SetChunkEntry> chunks;
};

// The transport refuses a message over 64 MiB and a dense chunk can encode to maxEncodedChunkBytes, so a message
// stays well inside that.
inline constexpr std::size_t maxSetChunksMessageBytes = 32 * 1024 * 1024;

// Splits the payload, in order, into terrainEdit messages of at most maxTerrainChunksEntries entries and at most
// maxSetChunksMessageBytes, counting every entry at its worst case. Empty for an empty payload.
[[nodiscard]] std::vector<net::Message> packSetChunksEdits(const TerrainSetChunks& payload, uint64_t strokeId = 0);

// Sets each entry on the volume the way the server will, without touching a revision.
void predictSetChunks(terrain::TerrainVolume& volume, const TerrainSetChunks& payload);

// Whether the two differ as the volume holds them: null and air are the same, and two held chunks differ by hash.
[[nodiscard]] bool terrainChunksDiffer(const std::shared_ptr<const terrain::TerrainChunk>& first,
                                       const std::shared_ptr<const terrain::TerrainChunk>& second);

}

#endif //TERRAINSETCHUNKS_H
