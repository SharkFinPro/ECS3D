#include "TerrainVolumeSource.h"
#include "TerrainSurfaceSampling.h"
#include <algorithm>
#include <utility>

TerrainVolumeSource::TerrainVolumeSource(std::shared_ptr<const TerrainSurfaceFeed> feed,
                                         const terrain::SampleBox& bounds)
  : TerrainVolumeSource(std::move(feed), bounds, bounds)
{}

TerrainVolumeSource::TerrainVolumeSource(std::shared_ptr<const TerrainSurfaceFeed> feed,
                                         const terrain::SampleBox& bounds, const terrain::SampleBox& sampleBounds)
  : m_feed(std::move(feed)), m_bounds{ bounds.min, bounds.max }, m_sampleBounds{ sampleBounds.min, sampleBounds.max }
{}

void TerrainVolumeSource::fill(const vke::VolumeRegion& region, const uint32_t lod, const std::span<float> distance,
                               const std::span<uint8_t> material) const
{
  const auto snapshot = m_feed->snapshot();
  const terrain::SampleBox box{ region.min, region.max };

  if (snapshot && terrain::fillSurfaceSamples(*snapshot, box, lod, distance, material))
  {
    return;
  }

  std::ranges::fill(distance, terrain::decodeDistance(terrain::airDistance));
  std::ranges::fill(material, uint8_t{ 0 });
}

vke::VolumeRegion TerrainVolumeSource::bounds() const
{
  return m_bounds;
}

vke::VolumeRegion TerrainVolumeSource::sampleBounds() const
{
  return m_sampleBounds;
}
