#ifndef TERRAINFORMAT_H
#define TERRAINFORMAT_H

#include "TerrainChunk.h"
#include "TerrainCodec.h"
#include "TerrainStore.h"
#include "TerrainVolume.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <uuid.h>
#include <vector>

namespace terrain
{
  // Chunks per region edge: a region file holds the chunks of one regionSize^3 block of chunk coordinates.
  inline constexpr int regionSize = 8;

  inline constexpr int terrainFormatVersion = 1;

  inline constexpr size_t regionHeaderBytes = 12;
  inline constexpr size_t entryHeaderBytes = 16;
  inline constexpr size_t maxRegionEntries = static_cast<size_t>(regionSize) * regionSize * regionSize;
  inline constexpr size_t maxRegionFileBytes =
    regionHeaderBytes + maxRegionEntries * (entryHeaderBytes + maxEncodedChunkBytes);
  inline constexpr uintmax_t maxIndexBytes = 8ull * 1024 * 1024;
  inline constexpr size_t maxRegions = 65536;

  inline constexpr const char* indexFileName = "terrain.json";

  using ChunkKey = std::array<int32_t, 3>;
  using HashByChunk = std::map<ChunkKey, uint64_t>;
  using ChunksByKey = std::map<ChunkKey, std::shared_ptr<const TerrainChunk>>;

  // What the index records about one region file: its chunk hashes, its name tag, its byte size and a hash of its bytes.
  struct RegionRecord
  {
    HashByChunk hashes;
    uint64_t tag = 0;
    uintmax_t bytes = 0;
    uint64_t fileHash = 0;
  };

  using RegionIndex = std::map<ChunkKey, RegionRecord>;

  struct TerrainIndex
  {
    TerrainDescription description;
    RegionIndex regions;
  };

  [[nodiscard]] ChunkKey regionOfChunk(const ChunkKey& chunk);

  // Names the exact chunk set a region file holds, and the codec that wrote it, so a file is never reused for
  // different content.
  [[nodiscard]] uint64_t regionTag(const HashByChunk& hashes);

  // The hash a region file's bytes are checked against, so damage that keeps the size is still caught.
  [[nodiscard]] uint64_t fileFingerprint(std::span<const uint8_t> bytes);

  // r.<x>.<y>.<z>.<16 lowercase hex>.bin
  [[nodiscard]] std::string regionFileName(const ChunkKey& region, uint64_t tag);

  // True only for a name regionFileName can produce, so a foreign file is never mistaken for one.
  [[nodiscard]] bool isRegionFileName(const std::string& name);

  // Empty when the chunks are none, too many, null, air, out of range or from more than one region.
  [[nodiscard]] std::optional<std::vector<uint8_t>> encodeRegion(const ChunksByKey& chunks);

  // Checks the bytes against the region and the index record, including the file hash. On failure the error names what is wrong, without a path
  // and chunks is untouched. Entries longer than maxEncodedChunkBytes are refused although decodeChunk accepts longer
  // non-minimal streams, because a file only holds this encoder's output.
  [[nodiscard]] bool decodeRegion(std::span<const uint8_t> bytes, const ChunkKey& region, const RegionRecord& record,
                                  TerrainVolume::ChunkMap& chunks, std::string& error);

  // Empty when parseIndex would refuse the text, such as an invalid description or a region with no chunks.
  [[nodiscard]] std::optional<std::string> writeIndex(const TerrainIndex& index);

  // The description's uuid is taken from expectedUuid, which the file must also state. chunkCount is left zero. On
  // failure index is untouched.
  [[nodiscard]] bool parseIndex(std::span<const uint8_t> text, const uuids::uuid& expectedUuid, TerrainIndex& index,
                                std::string& error);
}

#endif //TERRAINFORMAT_H
