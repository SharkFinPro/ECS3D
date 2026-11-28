#include "TerrainPalette.h"
#include "TerrainChunk.h"
#include "TerrainVolume.h"
#include <algorithm>
#include <utility>

namespace terrain
{
  namespace
  {
    std::optional<std::string> checkIndex(const std::vector<TerrainMaterial>& palette, const size_t index)
    {
      if (index >= palette.size())
      {
        return "Material " + std::to_string(index) + " does not exist";
      }

      return std::nullopt;
    }

    std::optional<std::string> checkName(const std::string& name)
    {
      if (name.size() > maxMaterialNameLength)
      {
        return "A material name is limited to " + std::to_string(maxMaterialNameLength) + " characters";
      }

      return std::nullopt;
    }
  }

  std::bitset<256> materialsInUse(const TerrainVolume& volume)
  {
    std::bitset<256> used;

    for (const auto& [coord, chunk] : volume.chunks())
    {
      if (!chunk)
      {
        continue;
      }

      const auto distances = chunk->distances();
      const auto materials = chunk->materials();

      // A uniform chunk's spans hold one shared sample; air samples carry no visible material.
      for (size_t i = 0; i < materials.size(); ++i)
      {
        if (distances[i] <= 0)
        {
          used.set(materials[i]);
        }
      }
    }

    return used;
  }

  std::optional<std::string> addMaterial(std::vector<TerrainMaterial>& palette, TerrainMaterial material)
  {
    if (palette.size() >= maxPaletteSize)
    {
      return "A terrain holds at most " + std::to_string(maxPaletteSize) + " materials";
    }

    if (const auto reason = checkName(material.name))
    {
      return reason;
    }

    palette.push_back(std::move(material));

    return std::nullopt;
  }

  std::optional<std::string> removeMaterial(std::vector<TerrainMaterial>& palette, const size_t index,
                                            const std::bitset<256>& inUse)
  {
    if (const auto reason = checkIndex(palette, index))
    {
      return reason;
    }

    for (size_t used = index; used < inUse.size(); ++used)
    {
      if (inUse.test(used))
      {
        return "Material " + std::to_string(used) + " is painted on the terrain";
      }
    }

    return eraseMaterial(palette, index);
  }

  std::optional<std::string> eraseMaterial(std::vector<TerrainMaterial>& palette, const size_t index)
  {
    if (const auto reason = checkIndex(palette, index))
    {
      return reason;
    }

    palette.erase(palette.begin() + static_cast<std::ptrdiff_t>(index));

    return std::nullopt;
  }

  std::optional<std::string> renameMaterial(std::vector<TerrainMaterial>& palette, const size_t index,
                                            const std::string& name)
  {
    if (const auto reason = checkIndex(palette, index))
    {
      return reason;
    }

    if (const auto reason = checkName(name))
    {
      return reason;
    }

    palette[index].name = name;

    return std::nullopt;
  }

  std::optional<std::string> moveMaterial(std::vector<TerrainMaterial>& palette, const size_t from, const size_t to)
  {
    if (const auto reason = checkIndex(palette, from))
    {
      return reason;
    }

    if (const auto reason = checkIndex(palette, to))
    {
      return reason;
    }

    if (from < to)
    {
      std::rotate(palette.begin() + static_cast<std::ptrdiff_t>(from),
                  palette.begin() + static_cast<std::ptrdiff_t>(from) + 1,
                  palette.begin() + static_cast<std::ptrdiff_t>(to) + 1);
    }
    else if (to < from)
    {
      std::rotate(palette.begin() + static_cast<std::ptrdiff_t>(to),
                  palette.begin() + static_cast<std::ptrdiff_t>(from),
                  palette.begin() + static_cast<std::ptrdiff_t>(from) + 1);
    }

    return std::nullopt;
  }

  std::optional<std::string> setMaterialTextures(std::vector<TerrainMaterial>& palette, const size_t index,
                                                 const uuids::uuid& albedo, const uuids::uuid& specular)
  {
    if (const auto reason = checkIndex(palette, index))
    {
      return reason;
    }

    palette[index].albedoTexture = albedo;
    palette[index].specularTexture = specular;

    return std::nullopt;
  }

  std::optional<std::string> setMaterialColor(std::vector<TerrainMaterial>& palette, const size_t index,
                                              const glm::vec3& color)
  {
    if (const auto reason = checkIndex(palette, index))
    {
      return reason;
    }

    if (!isValidColor(color))
    {
      return "A material color needs every component between 0 and 1";
    }

    palette[index].color = color;

    return std::nullopt;
  }
}
