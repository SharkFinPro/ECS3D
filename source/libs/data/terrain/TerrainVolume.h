#ifndef TERRAINVOLUME_H
#define TERRAINVOLUME_H

#include "TerrainChunk.h"
#include "TerrainTypes.h"
#include <glm/vec3.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace terrain
{
  class TerrainVolume
  {
  public:
    using ChunkMap = std::unordered_map<ChunkCoord, std::shared_ptr<const TerrainChunk>>;

    explicit TerrainVolume(float voxelSize = 2.5f);

    [[nodiscard]] float voxelSize() const;

    [[nodiscard]] std::shared_ptr<const TerrainChunk> chunk(ChunkCoord coord) const;

    // A null or air chunk erases the entry.
    void setChunk(ChunkCoord coord, std::shared_ptr<const TerrainChunk> chunk);

    [[nodiscard]] const ChunkMap& chunks() const;

    [[nodiscard]] ChunkMap snapshotChunks() const;

    void restoreChunks(ChunkMap chunks);

    [[nodiscard]] int16_t distanceAtSample(glm::ivec3 sample) const;

    [[nodiscard]] uint8_t materialAtSample(glm::ivec3 sample) const;

    [[nodiscard]] SampleBox occupiedBounds() const;

    [[nodiscard]] size_t byteSize() const;

  private:
    float m_voxelSize;
    ChunkMap m_chunks;
  };
}

#endif //TERRAINVOLUME_H
