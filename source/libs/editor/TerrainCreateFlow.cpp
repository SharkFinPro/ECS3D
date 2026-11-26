#include "TerrainCreateFlow.h"
#include "AssetCreateFlow.h"
#include <Replication.h>
#include <objects/components/collisions/Terrain.h>
#include <nlohmann/json.hpp>
#include <terrain/TerrainAddition.h>
#include <terrain/TerrainGenerators.h>
#include <terrain/TerrainOps.h>
#include <terrain/TerrainTypes.h>
#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <limits>
#include <span>
#include <system_error>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

namespace terrainCreate
{
  namespace
  {
    // Keeps a footprint far past any bound inside integer arithmetic.
    constexpr int64_t maxColumns = 1ll << 30;

    std::string tooLargeMessage(const int64_t chunks)
    {
      return "That terrain needs about " + std::to_string(chunks) + " chunks; the most one terrain may hold is " +
             std::to_string(maxChunks()) + ". Use a larger voxel size or a smaller footprint.";
    }

    bool voxelSizeInBounds(const float voxelSizeMeters)
    {
      const float world = worldUnits(voxelSizeMeters);
      return std::isfinite(world) && world >= terrain::minVoxelSize && world <= terrain::maxVoxelSize;
    }
  }

  int64_t maxChunks()
  {
    return std::min<int64_t>(terrain::maxEditChunks, terrain::maxAddedTerrainChunks);
  }

  int64_t columnsFor(const float footprintMeters, const float voxelSizeMeters)
  {
    if (!std::isfinite(footprintMeters) || !std::isfinite(voxelSizeMeters) || footprintMeters <= 0.0f ||
        voxelSizeMeters <= 0.0f)
    {
      return 0;
    }

    const double columns = std::round(static_cast<double>(footprintMeters) / static_cast<double>(voxelSizeMeters));

    return static_cast<int64_t>(std::clamp(columns, 1.0, static_cast<double>(maxColumns)));
  }

  int64_t estimatedChunks(const int64_t columns, const int64_t rows, const float voxelSize, const float lowestHeight,
                          const float highestHeight)
  {
    if (columns < 1 || rows < 1 || !(voxelSize > 0.0f) || !std::isfinite(lowestHeight) ||
        !std::isfinite(highestHeight))
    {
      return 0;
    }

    const double thickness = static_cast<double>(thicknessVoxels) * voxelSize;
    const double lowVoxels = std::floor((static_cast<double>(lowestHeight) - thickness) / voxelSize);
    const double highVoxels = std::ceil(static_cast<double>(highestHeight) / voxelSize + terrain::bandVoxels) + 1.0;

    constexpr double maxExtentVoxels = 1.0e9;
    if (!(std::abs(lowVoxels) < maxExtentVoxels) || !(std::abs(highVoxels) < maxExtentVoxels))
    {
      return std::numeric_limits<int64_t>::max();
    }

    const int64_t bottomChunk = terrain::floorDiv(static_cast<int32_t>(lowVoxels), terrain::chunkSize);
    const int64_t topChunk = terrain::floorDiv(static_cast<int32_t>(highVoxels) - 1, terrain::chunkSize);
    const double chunksX = static_cast<double>((columns + terrain::chunkSize - 1) / terrain::chunkSize);
    const double chunksZ = static_cast<double>((rows + terrain::chunkSize - 1) / terrain::chunkSize);
    const double total = chunksX * chunksZ * static_cast<double>(topChunk - bottomChunk + 1);

    return total >= 1.0e18 ? static_cast<int64_t>(1.0e18) : static_cast<int64_t>(total);
  }

  std::optional<std::string> refusal(const TerrainCreateRequest& request, const AssetRegistry& registry)
  {
    if (!AssetCreateFlow::nameIsValid(request.name) || assetNaming::isBlank(request.name))
    {
      return "Enter a valid name (no path characters).";
    }

    if (assetNaming::isNameTaken(registry, AssetType::Terrain, request.name))
    {
      return "A terrain named " + request.name + " already exists.";
    }

    if (!std::isfinite(request.voxelSizeMeters) || request.voxelSizeMeters <= 0.0f ||
        !voxelSizeInBounds(request.voxelSizeMeters))
    {
      return "The voxel size must be between " + std::to_string(terrain::minVoxelSize / worldUnitsPerMeter) +
             " and " + std::to_string(terrain::maxVoxelSize / worldUnitsPerMeter) + " meters.";
    }

    if (request.fromHeightmap)
    {
      if (request.heightmapPath.empty())
      {
        return "Choose a heightmap image.";
      }

      if (!std::isfinite(request.heightScaleMeters) || request.heightScaleMeters <= 0.0f)
      {
        return "The height scale must be a positive number of meters.";
      }

      return std::nullopt;
    }

    if (!std::isfinite(request.footprintMeters) || request.footprintMeters <= 0.0f)
    {
      return "The footprint must be a positive number of meters.";
    }

    if (request.footprintMeters < request.voxelSizeMeters)
    {
      return "The footprint must be at least one voxel.";
    }

    const float voxelSize = worldUnits(request.voxelSizeMeters);
    const int64_t columns = columnsFor(request.footprintMeters, request.voxelSizeMeters);
    const float ground = flatGroundVoxels * voxelSize;

    if (const int64_t chunks = estimatedChunks(columns, columns, voxelSize, ground, ground); chunks > maxChunks())
    {
      return tooLargeMessage(chunks);
    }

    return std::nullopt;
  }

  HeightmapDecode decodeHeightmap(const std::filesystem::path& file, const int64_t maxSamples)
  {
    std::error_code ec;
    const uintmax_t size = std::filesystem::file_size(file, ec);
    if (ec || size == 0)
    {
      return { std::nullopt, "The heightmap image could not be read." };
    }

    if (size > maxImageFileBytes)
    {
      return { std::nullopt, "The heightmap image file is too large." };
    }

    std::vector<stbi_uc> bytes(static_cast<size_t>(size));
    std::ifstream in(file, std::ios::binary);
    if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
    {
      return { std::nullopt, "The heightmap image could not be read." };
    }

    const int length = static_cast<int>(bytes.size());
    int width = 0;
    int height = 0;
    int components = 0;
    if (!stbi_info_from_memory(bytes.data(), length, &width, &height, &components) || width < 1 || height < 1)
    {
      return { std::nullopt, "The heightmap is not a supported image (use a grayscale PNG)." };
    }

    if (static_cast<int64_t>(width) * static_cast<int64_t>(height) > maxSamples)
    {
      return { std::nullopt, "The heightmap is " + std::to_string(width) + " x " + std::to_string(height) +
                               " pixels, which is too large for one terrain." };
    }

    stbi_us* decoded = stbi_load_16_from_memory(bytes.data(), length, &width, &height, &components, 1);
    if (decoded == nullptr)
    {
      return { std::nullopt, "The heightmap image could not be decoded." };
    }

    HeightmapImage image;
    image.width = width;
    image.height = height;
    image.pixels.assign(decoded, decoded + static_cast<size_t>(width) * static_cast<size_t>(height));
    stbi_image_free(decoded);

    return { std::move(image), {} };
  }

  nlohmann::json placementEdit(const uuids::uuid& objectUUID, const std::string& name,
                               const uuids::uuid& terrainUUID)
  {
    Terrain component;
    component.setTerrainUUID(terrainUUID);
    const nlohmann::json data = component.serialize();

    return replication::buildBatch({ replication::buildAddObject(name, nullptr, &objectUUID),
                                     replication::buildAddComponent(objectUUID, "Terrain", &data) });
  }

  TerrainCreateResult generateTerrain(const TerrainCreateRequest& request, const AssetRegistry& registry,
                                      const uuids::uuid& terrainUUID)
  {
    if (const auto reason = refusal(request, registry))
    {
      return { std::nullopt, *reason };
    }

    const float voxelSize = worldUnits(request.voxelSizeMeters);
    const float thickness = thicknessVoxels * voxelSize;

    std::optional<HeightmapImage> image;
    int64_t columns = columnsFor(request.footprintMeters, request.voxelSizeMeters);
    int64_t rows = columns;
    float lowest = flatGroundVoxels * voxelSize;
    float highest = lowest;
    const float heightScale = worldUnits(request.heightScaleMeters);

    if (request.fromHeightmap)
    {
      auto decoded = decodeHeightmap(std::filesystem::path(request.heightmapPath),
                                     maxChunks() * terrain::chunkSize * terrain::chunkSize);
      if (!decoded.image)
      {
        return { std::nullopt, decoded.error };
      }

      image = std::move(decoded.image);
      columns = image->width;
      rows = image->height;
      lowest = 0.0f;
      highest = heightScale;
    }

    if (const int64_t chunks = estimatedChunks(columns, rows, voxelSize, lowest, highest); chunks > maxChunks())
    {
      return { std::nullopt, tooLargeMessage(chunks) };
    }

    auto volume = std::make_shared<terrain::TerrainVolume>(voxelSize);

    try
    {
      const terrain::ChunkEdits edits =
        image ? terrain::fillFromHeightmap(*volume, { std::span<const uint16_t>(image->pixels), image->width,
                                                       image->height }, heightScale, thickness, material)
              : terrain::fillFlat(*volume, { static_cast<int>(columns), static_cast<int>(rows) }, lowest, thickness,
                                  material);

      if (edits.empty())
      {
        return { std::nullopt, "That terrain is too large to generate." };
      }

      terrain::commit(*volume, edits);
    }
    catch (const std::exception& e)
    {
      return { std::nullopt, std::string("The terrain could not be generated: ") + e.what() };
    }

    terrain::TerrainDescription description;
    description.uuid = terrainUUID;
    description.voxelSize = voxelSize;
    description.chunkCount = static_cast<uint32_t>(volume->chunks().size());

    if (description.chunkCount > terrain::maxAddedTerrainChunks || !terrain::isValidDescription(description))
    {
      return { std::nullopt, "That terrain is too large to announce." };
    }

    return { GeneratedTerrain{ std::move(description), std::move(volume), request.name }, {} };
  }
}
