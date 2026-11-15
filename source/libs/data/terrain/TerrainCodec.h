#ifndef TERRAINCODEC_H
#define TERRAINCODEC_H

#include "TerrainChunk.h"
#include "TerrainTypes.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace terrain
{
  // The longest literal run the dense format writes, in samples.
  inline constexpr size_t maxLiteralRunSamples = 128;

  // Worst case for one encoded chunk: the tag, then a distance plane and a material plane that never repeat, so each
  // is cut into full literal runs of one control byte plus the values.
  inline constexpr size_t maxEncodedChunkBytes =
    1 + (static_cast<size_t>(samplesPerChunk) / maxLiteralRunSamples) *
          ((1 + maxLiteralRunSamples * sizeof(int16_t)) + (1 + maxLiteralRunSamples));

  [[nodiscard]] std::vector<uint8_t> encodeChunk(const TerrainChunk& chunk);

  // Returns nullptr on any malformed input. The decoder accepts any valid stream, not only the one encodeChunk
  // writes: a dense tag whose content is uniform, or short runs written as literals, decode fine. A chunk's identity
  // is therefore TerrainChunk::hash(), not its encoded bytes.
  [[nodiscard]] std::shared_ptr<const TerrainChunk> decodeChunk(std::span<const uint8_t> bytes);
}

#endif //TERRAINCODEC_H
