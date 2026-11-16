#include "TerrainStreamPlan.h"
#include "TerrainCodec.h"
#include <tuple>
#include <utility>

namespace terrain
{
  bool TerrainStreamPlan::Key::operator<(const Key& other) const
  {
    return std::tie(distanceSquared, coord.z, coord.y, coord.x) <
           std::tie(other.distanceSquared, other.coord.z, other.coord.y, other.coord.x);
  }

  TerrainStreamPlan::TerrainStreamPlan(const TerrainVolume& volume, const glm::vec3 focus)
    : m_focus(focus)
  {
    for (const auto& entry : volume.chunks())
    {
      m_queue.insert(keyFor(entry.first));
    }
  }

  std::vector<StreamedChunk> TerrainStreamPlan::next(const TerrainVolume& volume, const size_t budgetBytes)
  {
    std::vector<StreamedChunk> batch;
    size_t used = streamMessageOverheadBytes;

    while (!m_queue.empty())
    {
      const ChunkCoord coord = m_queue.begin()->coord;

      std::shared_ptr<const TerrainChunk> chunk = volume.chunk(coord);

      if (!chunk)
      {
        chunk = TerrainChunk::uniform(airDistance, 0);
      }

      std::vector<uint8_t> encoded = encodeChunk(*chunk);
      const size_t entryBytes = streamEntryOverheadBytes + encoded.size();

      // A chunk that does not fit ends the batch rather than letting a smaller, farther one jump the order.
      if (!batch.empty() && used + entryBytes > budgetBytes)
      {
        break;
      }

      used += entryBytes;
      batch.push_back({ coord, std::move(chunk), std::move(encoded) });
      m_queue.erase(m_queue.begin());
    }

    return batch;
  }

  void TerrainStreamPlan::requeue(const ChunkCoord coord)
  {
    if (isValidChunkCoord(coord))
    {
      m_queue.insert(keyFor(coord));
    }
  }

  bool TerrainStreamPlan::done() const
  {
    return m_queue.empty();
  }

  size_t TerrainStreamPlan::pending() const
  {
    return m_queue.size();
  }

  TerrainStreamPlan::Key TerrainStreamPlan::keyFor(const ChunkCoord coord) const
  {
    constexpr double half = chunkSize / 2;
    const double dx = static_cast<double>(coord.x) * chunkSize + half - static_cast<double>(m_focus.x);
    const double dy = static_cast<double>(coord.y) * chunkSize + half - static_cast<double>(m_focus.y);
    const double dz = static_cast<double>(coord.z) * chunkSize + half - static_cast<double>(m_focus.z);

    return { dx * dx + dy * dy + dz * dz, coord };
  }
}
