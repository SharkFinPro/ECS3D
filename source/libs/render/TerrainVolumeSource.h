#ifndef TERRAINVOLUMESOURCE_H
#define TERRAINVOLUMESOURCE_H

#include <terrain/TerrainTypes.h>
#include <terrain/TerrainVolume.h>
#include <VulkanEngine/components/assets/volume/VolumeSource.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <span>

// The chunk snapshot a terrain's surfaces read, shared by all of them. Fills copy the pointer under a shared lock and
// read the immutable chunks without it; publish swaps the pointer and runs the invalidations under the exclusive
// lock, so no fill can see a new snapshot before the surfaces are told about it.
class TerrainSurfaceFeed
{
public:
  explicit TerrainSurfaceFeed(std::shared_ptr<const terrain::TerrainVolume::ChunkMap> snapshot);

  [[nodiscard]] std::shared_ptr<const terrain::TerrainVolume::ChunkMap> snapshot() const;

  void publish(std::shared_ptr<const terrain::TerrainVolume::ChunkMap> snapshot, const std::function<void()>& report);

private:
  mutable std::shared_mutex m_mutex;
  std::shared_ptr<const terrain::TerrainVolume::ChunkMap> m_snapshot;
};

// Answers a surface's fills from the feed, inside fixed bounds.
class TerrainVolumeSource final : public vke::VolumeSource
{
public:
  TerrainVolumeSource(std::shared_ptr<const TerrainSurfaceFeed> feed, const terrain::SampleBox& bounds);

  void fill(const vke::VolumeRegion& region, uint32_t lod, std::span<float> distance,
            std::span<uint8_t> material) const override;

  [[nodiscard]] vke::VolumeRegion bounds() const override;

private:
  std::shared_ptr<const TerrainSurfaceFeed> m_feed;
  vke::VolumeRegion m_bounds;
};

#endif //TERRAINVOLUMESOURCE_H
