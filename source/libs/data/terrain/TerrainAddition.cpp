#include "TerrainAddition.h"
#include "assets/AssetRegistry.h"
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>
#include <uuid.h>

namespace terrain
{
  namespace
  {
    using Json = nlohmann::json;

    bool readUuid(const Json& value, uuids::uuid& uuid)
    {
      if (!value.is_string())
      {
        return false;
      }

      const auto parsed = uuids::uuid::from_string(value.get<std::string>());
      if (!parsed.has_value())
      {
        return false;
      }

      uuid = *parsed;
      return true;
    }

    // A missing "color" is white, so files and bodies written before it existed still load.
    bool readColor(const Json& entry, glm::vec3& color)
    {
      if (!entry.contains("color"))
      {
        return true;
      }

      const Json& value = entry["color"];
      if (!value.is_array() || value.size() != 3)
      {
        return false;
      }

      for (size_t i = 0; i < 3; ++i)
      {
        if (!value[i].is_number())
        {
          return false;
        }

        color[static_cast<glm::length_t>(i)] = static_cast<float>(value[i].get<double>());
      }

      return true;
    }

    bool readPalette(const Json& value, std::vector<TerrainMaterial>& palette)
    {
      if (!value.is_array() || value.size() > maxPaletteSize)
      {
        return false;
      }

      for (const Json& entry : value)
      {
        TerrainMaterial material;

        if (!entry.is_object() || !entry.contains("name") || !entry["name"].is_string() ||
            !entry.contains("albedoTexture") || !readUuid(entry["albedoTexture"], material.albedoTexture) ||
            !entry.contains("specularTexture") || !readUuid(entry["specularTexture"], material.specularTexture) ||
            !readColor(entry, material.color))
        {
          return false;
        }

        material.name = entry["name"].get<std::string>();
        palette.push_back(std::move(material));
      }

      return true;
    }
  }

  Json addTerrainAssetJson(const TerrainDescription& description)
  {
    Json body;
    body["voxelSize"] = description.voxelSize;
    body["chunkCount"] = description.chunkCount;
    body["palette"] = Json::array();

    for (const TerrainMaterial& material : description.palette)
    {
      body["palette"].push_back({ { "name", material.name },
                                  { "albedoTexture", uuids::to_string(material.albedoTexture) },
                                  { "specularTexture", uuids::to_string(material.specularTexture) },
                                  { "color", Json::array({ material.color.r, material.color.g, material.color.b }) } });
    }

    return {
      { "assetType", "terrain" },
      { "uuid", uuids::to_string(description.uuid) },
      { "path", terrainAssetPath(description.uuid) },
      { "body", body.dump() }
    };
  }

  bool hasAddedTerrainBody(const Json& asset)
  {
    return asset.is_object() && asset.value("assetType", std::string{}) == "terrain" &&
           !asset.value("body", std::string{}).empty();
  }

  std::optional<TerrainDescription> parseAddedTerrain(const Json& asset)
  {
    if (!hasAddedTerrainBody(asset))
    {
      return std::nullopt;
    }

    TerrainDescription description;
    const auto uuid = uuids::uuid::from_string(asset.value("uuid", std::string{}));
    if (!uuid.has_value())
    {
      return std::nullopt;
    }

    description.uuid = *uuid;

    const Json body = Json::parse(asset.value("body", std::string{}), nullptr, false);
    if (!body.is_object() || !body.contains("voxelSize") || !body["voxelSize"].is_number() ||
        !body.contains("chunkCount") || !body["chunkCount"].is_number_unsigned() || !body.contains("palette") ||
        !readPalette(body["palette"], description.palette))
    {
      return std::nullopt;
    }

    const uint64_t chunkCount = body["chunkCount"].get<uint64_t>();
    if (chunkCount > maxAddedTerrainChunks)
    {
      return std::nullopt;
    }

    const double voxelSize = body["voxelSize"].get<double>();
    if (!std::isfinite(voxelSize) || voxelSize < minVoxelSize || voxelSize > maxVoxelSize)
    {
      return std::nullopt;
    }

    description.voxelSize = static_cast<float>(voxelSize);
    description.chunkCount = static_cast<uint32_t>(chunkCount);

    if (!isValidDescription(description))
    {
      return std::nullopt;
    }

    return description;
  }

  bool applyAddedTerrain(TerrainStore& store, const TerrainDescription& description, const bool expectUpload)
  {
    if (store.create(description) == nullptr)
    {
      return false;
    }

    return !expectUpload || description.chunkCount == 0 || store.expectChunks(description.uuid, description.chunkCount);
  }
}
