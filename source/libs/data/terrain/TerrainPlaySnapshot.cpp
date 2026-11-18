#include "TerrainPlaySnapshot.h"
#include <algorithm>
#include <tuple>

namespace terrain
{
  void TerrainPlaySnapshot::capture(const TerrainStore& store)
  {
    m_terrains.clear();

    for (const auto& uuid : store.terrains())
    {
      if (const auto volume = store.find(uuid))
      {
        m_terrains[uuid] = { volume->voxelSize(), volume->snapshotChunks() };
      }
    }
  }

  void TerrainPlaySnapshot::clear()
  {
    m_terrains.clear();
  }

  bool TerrainPlaySnapshot::empty() const
  {
    return m_terrains.empty();
  }

  std::vector<std::pair<uuids::uuid, std::vector<ChunkCoord>>> TerrainPlaySnapshot::restore(TerrainStore& store)
  {
    std::vector<std::pair<uuids::uuid, std::vector<ChunkCoord>>> restored;

    for (const auto& [uuid, captured] : m_terrains)
    {
      const auto volume = store.find(uuid);
      if (!volume || volume->voxelSize() != captured.voxelSize)
      {
        continue;
      }

      const auto current = volume->snapshotChunks();
      std::vector<ChunkCoord> coords;

      for (const auto& [coord, chunk] : captured.chunks)
      {
        const auto found = current.find(coord);
        if (found == current.end() || found->second != chunk)
        {
          coords.push_back(coord);
        }
      }

      for (const auto& entry : current)
      {
        if (!captured.chunks.contains(entry.first))
        {
          coords.push_back(entry.first);
        }
      }

      std::ranges::sort(coords, [](const ChunkCoord& a, const ChunkCoord& b)
      {
        return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z);
      });

      for (const auto coord : coords)
      {
        const auto found = captured.chunks.find(coord);
        store.setChunk(uuid, coord, found == captured.chunks.end() ? nullptr : found->second,
                       store.nextRevision(uuid));
      }

      if (!coords.empty())
      {
        restored.emplace_back(uuid, std::move(coords));
      }
    }

    m_terrains.clear();
    return restored;
  }
}