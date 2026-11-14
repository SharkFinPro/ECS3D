#ifndef TERRAINCODEC_H
#define TERRAINCODEC_H

#include "TerrainChunk.h"
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace terrain
{
  [[nodiscard]] std::vector<uint8_t> encodeChunk(const TerrainChunk& chunk);

  // Returns nullptr on any malformed input. The decoder accepts any valid stream, not only the one encodeChunk
  // writes: a dense tag whose content is uniform, or short runs written as literals, decode fine. A chunk's identity
  // is therefore TerrainChunk::hash(), not its encoded bytes.
  [[nodiscard]] std::shared_ptr<const TerrainChunk> decodeChunk(std::span<const uint8_t> bytes);
}

#endif //TERRAINCODEC_H
