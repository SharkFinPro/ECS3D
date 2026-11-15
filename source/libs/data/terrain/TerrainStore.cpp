#include "TerrainStore.h"
#include "assets/AssetRegistry.h"
#include <algorithm>
#include <cmath>

namespace terrain
{
  namespace
  {
    constexpr float maxVoxelSize = 1000.0f;
  }

  bool isValidDescription(const TerrainDescription& description)
  {
    if (description.uuid.is_nil() || !std::isfinite(description.voxelSize) || description.voxelSize <= 0.0f ||
        description.voxelSize > maxVoxelSize || description.palette.size() > maxPaletteSize)
    {
      return false;
    }

    return std::ranges::all_of(description.palette, [](const TerrainMaterial& material) {
      return material.name.size() <= maxMaterialNameLength;
    });
  }

  std::shared_ptr<TerrainVolume> TerrainStore::create(const TerrainDescription& description)
  {
    if (!isValidDescription(description) || m_terrains.contains(description.uuid))
    {
      return nullptr;
    }

    auto volume = std::make_shared<TerrainVolume>(description.voxelSize);
    m_terrains.emplace(description.uuid, Entry{ description, volume });

    return volume;
  }

  std::shared_ptr<TerrainVolume> TerrainStore::find(const uuids::uuid& uuid) const
  {
    const auto it = m_terrains.find(uuid);

    return it != m_terrains.end() ? it->second.volume : nullptr;
  }

  const TerrainDescription* TerrainStore::description(const uuids::uuid& uuid) const
  {
    const auto it = m_terrains.find(uuid);

    return it != m_terrains.end() ? &it->second.description : nullptr;
  }

  bool TerrainStore::remove(const uuids::uuid& uuid)
  {
    return m_terrains.erase(uuid) > 0;
  }

  std::vector<uuids::uuid> TerrainStore::terrains() const
  {
    std::vector<uuids::uuid> result;
    result.reserve(m_terrains.size());

    for (const auto& [uuid, entry] : m_terrains)
    {
      result.push_back(uuid);
    }

    std::sort(result.begin(), result.end());

    return result;
  }

  void TerrainStore::reconcile(const AssetRegistry& registry)
  {
    std::erase_if(m_terrains, [&registry](const auto& item) {
      return registry.getByUUIDOfType(item.first, AssetType::Terrain) == nullptr;
    });

    for (const auto& [uuid, record] : registry.getAssets())
    {
      if (record.type == AssetType::Terrain && !m_terrains.contains(uuid))
      {
        create({ .uuid = uuid });
      }
    }
  }
}
