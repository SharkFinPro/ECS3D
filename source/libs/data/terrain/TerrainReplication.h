#ifndef TERRAINREPLICATION_H
#define TERRAINREPLICATION_H

#include "TerrainChunk.h"
#include "TerrainStore.h"
#include "TerrainTypes.h"
#include <Protocol.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <uuid.h>
#include <vector>

namespace terrain
{
  struct ChunkEntry
  {
    ChunkCoord coord;
    uint32_t revision = 0;
    uint64_t hash = 0;
    // encodeChunk output; empty for a hash-only entry.
    std::vector<uint8_t> encoded;
  };

  // Encodes the chunk; a null chunk is sent as the air chunk, so a receiver erases the coordinate.
  [[nodiscard]] ChunkEntry makeChunkEntry(ChunkCoord coord, uint32_t revision,
                                          const std::shared_ptr<const TerrainChunk>& chunk);

  // "You should already hold the chunk with this hash."
  [[nodiscard]] ChunkEntry makeHashOnlyEntry(ChunkCoord coord, uint32_t revision, uint64_t hash);

  // Fixed bytes per entry before its encoded bytes (3 x int32, revision, hash, size), and the payload header (terrain
  // uuid, entry count). A sender sizing batches against a byte budget uses these.
  inline constexpr size_t chunkEntryHeaderBytes =
    3 * sizeof(int32_t) + sizeof(uint32_t) + sizeof(uint64_t) + sizeof(uint32_t);
  inline constexpr size_t terrainChunksHeaderBytes = 16 + sizeof(uint32_t);

  [[nodiscard]] net::Message packTerrainChunks(const uuids::uuid& terrain, std::span<const ChunkEntry> entries);

  struct ReceivedChunk
  {
    ChunkCoord coord;
    uint32_t revision = 0;
    uint64_t hash = 0;
    // Null for a hash-only entry.
    std::shared_ptr<const TerrainChunk> chunk;
  };

  struct TerrainChunksPayload
  {
    uuids::uuid terrain;
    std::vector<ReceivedChunk> entries;
  };

  // nullopt for any malformed payload: wrong message type, truncated, nil uuid, a count the remaining bytes cannot
  // hold, a coordinate beyond maxChunkCoord, a size over maxEncodedChunkBytes or past the end, bytes decodeChunk
  // refuses, a decoded chunk whose hash() differs from the entry's hash, a coordinate repeated in one message,
  // revision 0, or trailing bytes.
  [[nodiscard]] std::optional<TerrainChunksPayload> unpackTerrainChunks(const net::Message& message);

  struct ChunksApplyResult
  {
    bool unknownTerrain = false;
    size_t applied = 0;
    // Entries whose revision was not higher than the one held.
    size_t stale = 0;
    // Hash-only entries the store could not satisfy (no matching chunk held); the receiver must ask for or wait for
    // the full chunk. Their revision is not recorded.
    std::vector<ChunkCoord> missing;
  };

  // The view side: applies each entry whose revision is higher than the store's for that coordinate. A full entry
  // replaces the chunk. A hash-only entry is accepted (revision recorded, chunk kept) only when the held chunk's hash
  // equals it - an absent coordinate holds the air chunk, so it compares against the air chunk's hash.
  ChunksApplyResult applyTerrainChunks(TerrainStore& store, const TerrainChunksPayload& payload);
}

#endif //TERRAINREPLICATION_H
