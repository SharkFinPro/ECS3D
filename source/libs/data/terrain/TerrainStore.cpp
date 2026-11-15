#include "TerrainStore.h"
#include "assets/AssetRegistry.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace terrain
{
  namespace
  {
    // Material names reach JSON on save and json::dump throws on invalid UTF-8, so refuse it here.
    bool isValidName(const std::string& name)
    {
      const size_t size = name.size();
      size_t i = 0;

      while (i < size)
      {
        const auto lead = static_cast<unsigned char>(name[i]);

        if (lead < 0x20 || lead == 0x7F)
        {
          return false;
        }

        if (lead < 0x80)
        {
          ++i;
          continue;
        }

        size_t length = 0;
        uint32_t codePoint = 0;
        uint32_t minimum = 0;

        if (lead >= 0xC2 && lead <= 0xDF)
        {
          length = 2;
          codePoint = lead & 0x1Fu;
          minimum = 0x80;
        }
        else if (lead >= 0xE0 && lead <= 0xEF)
        {
          length = 3;
          codePoint = lead & 0x0Fu;
          minimum = 0x800;
        }
        else if (lead >= 0xF0 && lead <= 0xF4)
        {
          length = 4;
          codePoint = lead & 0x07u;
          minimum = 0x10000;
        }
        else
        {
          return false;
        }

        if (i + length > size)
        {
          return false;
        }

        for (size_t k = 1; k < length; ++k)
        {
          const auto next = static_cast<unsigned char>(name[i + k]);
          if ((next & 0xC0) != 0x80)
          {
            return false;
          }

          codePoint = (codePoint << 6) | (next & 0x3Fu);
        }

        if (codePoint < minimum || codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF))
        {
          return false;
        }

        i += length;
      }

      return true;
    }
  }

  bool isValidDescription(const TerrainDescription& description)
  {
    if (description.uuid.is_nil() || !std::isfinite(description.voxelSize) || description.voxelSize < minVoxelSize ||
        description.voxelSize > maxVoxelSize || description.palette.size() > maxPaletteSize)
    {
      return false;
    }

    return std::ranges::all_of(description.palette, [](const TerrainMaterial& material) {
      return material.name.size() <= maxMaterialNameLength && isValidName(material.name);
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

  bool TerrainStore::adopt(const TerrainDescription& description, std::shared_ptr<TerrainVolume> volume)
  {
    if (!isValidDescription(description) || volume == nullptr || volume->voxelSize() != description.voxelSize)
    {
      return false;
    }

    m_terrains.insert_or_assign(description.uuid, Entry{ description, std::move(volume) });

    return true;
  }

  bool TerrainStore::remove(const uuids::uuid& uuid)
  {
    return m_terrains.erase(uuid) > 0;
  }

  bool TerrainStore::applyDescription(const TerrainDescription& description)
  {
    const auto it = m_terrains.find(description.uuid);
    if (it == m_terrains.end() || !isValidDescription(description))
    {
      return false;
    }

    Entry& entry = it->second;
    if (entry.description.voxelSize != description.voxelSize)
    {
      entry.volume = std::make_shared<TerrainVolume>(description.voxelSize);
    }

    entry.description = description;

    return true;
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
