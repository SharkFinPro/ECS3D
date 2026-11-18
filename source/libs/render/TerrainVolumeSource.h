#ifndef TERRAINVOLUMESOURCE_H
#define TERRAINVOLUMESOURCE_H

#include "TerrainSurfaceFeed.h"
#include <terrain/TerrainTypes.h>
#include <VulkanEngine/components/assets/volume/VolumeSource.h>
#include <cstdint>
#include <memory>
#include <span>

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
