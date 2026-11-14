#ifndef TERRAINCHUNK_H
#define TERRAINCHUNK_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace terrain
{
  class TerrainChunk
  {
  public:
    [[nodiscard]] static std::shared_ptr<const TerrainChunk> uniform(int16_t distance, uint8_t material);

    [[nodiscard]] static std::shared_ptr<const TerrainChunk> dense(std::vector<int16_t> distances,
                                                                   std::vector<uint8_t> materials);

    [[nodiscard]] bool isUniform() const;

    [[nodiscard]] bool isAir() const;

    [[nodiscard]] int16_t distanceAt(int x, int y, int z) const;

    [[nodiscard]] uint8_t materialAt(int x, int y, int z) const;

    [[nodiscard]] int16_t distanceAtIndex(int index) const;

    [[nodiscard]] uint8_t materialAtIndex(int index) const;

    // A uniform chunk's spans hold the one shared value, so their length is 1.
    [[nodiscard]] std::span<const int16_t> distances() const;

    [[nodiscard]] std::span<const uint8_t> materials() const;

    [[nodiscard]] uint64_t hash() const;

    [[nodiscard]] size_t byteSize() const;

  private:
    TerrainChunk(std::vector<int16_t> distances, std::vector<uint8_t> materials, bool isUniform);

    std::vector<int16_t> m_distances;
    std::vector<uint8_t> m_materials;
    bool m_isUniform;
    uint64_t m_hash = 0;
  };
}

#endif //TERRAINCHUNK_H
