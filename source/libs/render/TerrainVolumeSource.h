#ifndef TERRAINVOLUMESOURCE_H
#define TERRAINVOLUMESOURCE_H

#include "TerrainSurfaceFeed.h"
#include <terrain/TerrainTypes.h>
#include <VulkanEngine/components/assets/volume/VolumeSource.h>
#include <cstdint>
#include <memory>
#include <span>

// Answers a surface's fills from the feed. Cells are meshed inside the bounds; the sample bounds (the bounds unless
// given larger) are the samples it answers, which only shape the normals at the bounds' faces.
class TerrainVolumeSource final : public vke::VolumeSource
{
public:
  TerrainVolumeSource(std::shared_ptr<const TerrainSurfaceFeed> feed, const terrain::SampleBox& bounds);

  TerrainVolumeSource(std::shared_ptr<const TerrainSurfaceFeed> feed, const terrain::SampleBox& bounds,
                      const terrain::SampleBox& sampleBounds);

  void fill(const vke::VolumeRegion& region, uint32_t lod, std::span<float> distance,
            std::span<uint8_t> material) const override;

  [[nodiscard]] vke::VolumeRegion bounds() const override;

  [[nodiscard]] vke::VolumeRegion sampleBounds() const override;

private:
  std::shared_ptr<const TerrainSurfaceFeed> m_feed;
  vke::VolumeRegion m_bounds;
  vke::VolumeRegion m_sampleBounds;
};

#endif //TERRAINVOLUMESOURCE_H
