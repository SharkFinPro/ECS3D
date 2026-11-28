#ifndef TERRAINEDIT_H
#define TERRAINEDIT_H

#include "TerrainBrush.h"
#include "TerrainChunk.h"
#include "TerrainMaterialRemap.h"
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
  enum class TerrainEditKind : uint8_t { brush, redistance, setChunks, remapMaterials };

  struct SetChunkEntry
  {
    ChunkCoord coord;
    // Null means air.
    std::shared_ptr<const TerrainChunk> chunk;
  };

  struct TerrainEditPayload
  {
    uuids::uuid terrain;
    uint64_t strokeId = 0;
    TerrainEditKind kind = TerrainEditKind::brush;
    // brush only
    BrushOp brush;
    // redistance only
    SampleBox box;
    // setChunks only; a decoded air chunk stays a (non-null) air chunk.
    std::vector<SetChunkEntry> chunks;
    // remapMaterials only
    MaterialRemap remap = identityRemap();
  };

  // Fixed bytes per setChunks entry before its encoded bytes: 3 x int32, hash, size.
  inline constexpr size_t setChunkEntryHeaderBytes =
    3 * sizeof(int32_t) + sizeof(uint64_t) + sizeof(uint32_t);

  [[nodiscard]] net::Message packBrushEdit(const uuids::uuid& terrain, uint64_t strokeId, const BrushOp& op);

  [[nodiscard]] net::Message packRedistanceEdit(const uuids::uuid& terrain, uint64_t strokeId, const SampleBox& box);

  // Throws std::length_error for more than maxTerrainChunksEntries entries.
  [[nodiscard]] net::Message packSetChunksEdit(const uuids::uuid& terrain, uint64_t strokeId,
                                               std::span<const SetChunkEntry> chunks);

  // Remaps every sample of the terrain on the server, for a palette removal or move that keeps painted ground looking
  // the same.
  [[nodiscard]] net::Message packRemapEdit(const uuids::uuid& terrain, uint64_t strokeId, const MaterialRemap& remap);

  // nullopt for any malformed payload: wrong message type, truncated, nil uuid, an unknown kind, a brush a reader
  // cannot trust, an empty redistance box, a setChunks count of 0, above maxTerrainChunksEntries or one the remaining
  // bytes cannot hold, a coordinate beyond maxChunkCoord or repeated in one message, a size over maxEncodedChunkBytes,
  // bytes decodeChunk refuses (an empty entry included), a decoded chunk whose hash() differs from the entry's hash, a
  // remap isValidRemap refuses or the identity (an edit that can change nothing), or trailing bytes. Limits that depend on the volume (brush radius, box size) are the apply step's to enforce.
  [[nodiscard]] std::optional<TerrainEditPayload> unpackTerrainEdit(const net::Message& message);
}

#endif //TERRAINEDIT_H
