#include "TerrainVolumeSource.h"
#include "TerrainSurfaceSampling.h"
#include <algorithm>
#include <mutex>
#include <utility>

TerrainSurfaceFeed::TerrainSurfaceFeed(std::shared_ptr<const terrain::TerrainVolume::ChunkMap> snapshot)
  : m_snapshot(std::move(snapshot))
{}

std::shared_ptr<const terrain::TerrainVolume::ChunkMap> TerrainSurfaceFeed::snapshot() const
{
  const std::shared_lock lock(m_mutex);

  return m_snapshot;
}

void TerrainSurfaceFeed::publish(std::shared_ptr<const terrain::TerrainVolume::ChunkMap> snapshot,
                                 const std::function<void()>& report)
{
  const std::unique_lock lock(m_mutex);

  m_snapshot = std::move(snapshot);

  if (report)
  {
    report();
  }
}

TerrainVolumeSource::TerrainVolumeSource(std::shared_ptr<const TerrainSurfaceFeed> feed,
                                         const terrain::SampleBox& bounds)
  : m_feed(std::move(feed)), m_bounds{ bounds.min, bounds.max }
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
