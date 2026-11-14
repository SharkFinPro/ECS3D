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

  // Returns nullptr on any malformed input.
  [[nodiscard]] std::shared_ptr<const TerrainChunk> decodeChunk(std::span<const uint8_t> bytes);
}

#endif //TERRAINCODEC_H
