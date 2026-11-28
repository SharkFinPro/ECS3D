#include "TerrainMaterialRemap.h"
#include "TerrainChunk.h"
#include "TerrainParallel.h"
#include "TerrainStore.h"
#include "TerrainVolume.h"
#include <algorithm>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

namespace terrain
{
  namespace
  {
    std::shared_ptr<const TerrainChunk> remapChunk(const TerrainChunk& chunk, const MaterialRemap& remap)
    {
      const auto materials = chunk.materials();

      if (std::ranges::all_of(materials, [&remap](const uint8_t material) { return remap[material] == material; }))
      {
        return nullptr;
      }

      if (chunk.isUniform())
      {
        return TerrainChunk::uniform(chunk.distances().front(), remap[materials.front()]);
      }

      std::vector<uint8_t> remapped(materials.size());
      std::ranges::transform(materials, remapped.begin(), [&remap](const uint8_t material) { return remap[material]; });

      return TerrainChunk::dense(std::vector<int16_t>(chunk.distances().begin(), chunk.distances().end()),
                                 std::move(remapped));
    }
  }

  bool isIdentity(const MaterialRemap& remap)
  {
    return remap == identityRemap();
  }

  bool isValidRemap(const MaterialRemap& remap)
  {
    for (size_t index = 0; index < remap.size(); ++index)
    {
      if ((index == reservedMaterial) != (remap[index] == reservedMaterial))
      {
        return false;
      }
    }

    return true;
  }

  std::optional<MaterialRemap> remapForRemoval(const size_t paletteSize, const size_t removed,
                                               const size_t replacement)
  {
    if (paletteSize > maxPaletteSize || removed >= paletteSize || replacement >= paletteSize || replacement == removed)
    {
      return std::nullopt;
    }

    MaterialRemap remap = identityRemap();

    for (size_t index = removed + 1; index < reservedMaterial; ++index)
    {
      remap[index] = static_cast<uint8_t>(index - 1);
    }

    remap[removed] = static_cast<uint8_t>(replacement < removed ? replacement : replacement - 1);

    return remap;
  }

  std::optional<MaterialRemap> remapForMove(const size_t paletteSize, const size_t from, const size_t to)
  {
    if (paletteSize > maxPaletteSize || from >= paletteSize || to >= paletteSize)
    {
      return std::nullopt;
    }

    MaterialRemap remap = identityRemap();

    for (size_t index = std::min(from, to); index <= std::max(from, to); ++index)
    {
      remap[index] = static_cast<uint8_t>(from < to ? index - 1 : index + 1);
    }

    remap[from] = static_cast<uint8_t>(to);

    return remap;
  }

  ChunkEdits remapMaterials(const TerrainVolume& volume, const MaterialRemap& remap)
  {
    if (!isValidRemap(remap) || isIdentity(remap))
    {
      return {};
    }

    std::vector<std::pair<ChunkCoord, std::shared_ptr<const TerrainChunk>>> held(volume.chunks().begin(),
                                                                                 volume.chunks().end());
    std::ranges::sort(held, [](const auto& a, const auto& b)
    {
      return std::tie(a.first.z, a.first.y, a.first.x) < std::tie(b.first.z, b.first.y, b.first.x);
    });

    std::vector<std::shared_ptr<const TerrainChunk>> remapped(held.size());

    parallelFor(static_cast<int>(held.size()), [&](const int index)
    {
      const auto& chunk = held[static_cast<size_t>(index)].second;
      if (chunk)
      {
        remapped[static_cast<size_t>(index)] = remapChunk(*chunk, remap);
      }
    });

    ChunkEdits edits;

    for (size_t index = 0; index < held.size(); ++index)
    {
      if (remapped[index])
      {
        edits.emplace_back(held[index].first, std::move(remapped[index]));
      }
    }

    return edits;
  }

  ChunkEdits commitRemap(TerrainVolume& volume, const MaterialRemap& remap)
  {
    auto edits = remapMaterials(volume, remap);
    commit(volume, edits);
    return edits;
  }
}
