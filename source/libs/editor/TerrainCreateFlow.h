#ifndef TERRAINCREATEFLOW_H
#define TERRAINCREATEFLOW_H

#include "AssetNaming.h"
#include <WorldUnits.h>
#include <assets/AssetRegistry.h>
#include <VulkanEngine/components/assets/volume/VolumeMaterial.h>
#include <glm/vec3.hpp>
#include <nlohmann/json_fwd.hpp>
#include <terrain/TerrainAddition.h>
#include <terrain/TerrainOps.h>
#include <terrain/TerrainStore.h>
#include <terrain/TerrainTypes.h>
#include <terrain/TerrainVolume.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <uuid.h>

// The decisions behind the asset browser's "New Terrain" form: the request, its validation, the unit conversion, the
// heightmap decode and the generated volume. These are pure functions: a UI calls them and owns drawing and sending.
struct TerrainCreateRequest {
  std::string name;
  float footprintMeters = 128.0f;
  float voxelSizeMeters = 0.5f;
  bool fromHeightmap = false;
  // The image a heightmap terrain is built from; its size, times the voxel size, is the footprint.
  std::string heightmapPath;
  float heightScaleMeters = 32.0f;
};

struct HeightmapImage {
  std::vector<uint16_t> pixels; // row-major, width * height entries
  int width = 0;
  int height = 0;
};

struct HeightmapDecode {
  std::optional<HeightmapImage> image;
  std::string error;
};

// A terrain ready to be announced: its description (with the chunk count of the volume) and the volume itself.
struct GeneratedTerrain {
  terrain::TerrainDescription description;
  std::shared_ptr<terrain::TerrainVolume> volume;
  std::string displayName;
};

struct TerrainCreateResult {
  std::optional<GeneratedTerrain> terrain;
  std::string error;
};

namespace terrainCreate {
  // Flat ground stands this many voxels above the volume's floor, over a solid layer this many voxels thick, so a
  // flat terrain is one chunk tall at any voxel size.
  inline constexpr float flatGroundVoxels = 8.0f;
  inline constexpr float thicknessVoxels = 4.0f;
  inline constexpr uint8_t material = 0;

  // The one palette entry a created terrain starts with, without textures.
  inline constexpr const char* defaultMaterialName = "Ground";
  // The renderer's first default color, so a new terrain draws gray.
  inline const glm::vec3 defaultMaterialColor = vke::s_defaultVolumeColors[0];

  // The most chunks the form creates, since generation runs on the frame that presses Create. Well under what one
  // generator call (maxEditChunks) and one addAsset (maxAddedTerrainChunks) accept, which the server keeps.
  inline constexpr int64_t maxCreateChunks = 4096;
  static_assert(maxCreateChunks <= terrain::maxEditChunks && maxCreateChunks <= terrain::maxAddedTerrainChunks);

  // A heightmap covers at least one chunk column per 32 x 32 samples, so more samples than this cannot fit.
  inline constexpr int64_t maxImageSamples = maxCreateChunks * terrain::chunkSize * terrain::chunkSize;

  // The largest image file read: room for a 16-bit RGBA image of the most samples, plus metadata.
  inline constexpr uintmax_t maxImageFileBytes = static_cast<uintmax_t>(maxImageSamples) * 8u + (1u << 20);

  // The most memory a chunk holds: a distance and a material per sample.
  inline constexpr int64_t maxChunkBytes = static_cast<int64_t>(terrain::samplesPerChunk) * 3;

  [[nodiscard]] inline float worldUnits(const float meters)
  {
    return meters * worldUnitsPerMeter;
  }

  // Sample columns along a footprint, never fewer than one. Not finite or non-positive arguments give 0.
  [[nodiscard]] int64_t columnsFor(float footprintMeters, float voxelSizeMeters);

  // The chunks the generators touch for a columns x rows area whose surface lies between lowestHeight and
  // highestHeight (terrain-local units), assuming a flat surface for the band above it. A heightmap's slopes can
  // only add chunks, which the generator itself refuses past maxEditChunks.
  [[nodiscard]] int64_t estimatedChunks(int64_t columns, int64_t rows, float voxelSize, float lowestHeight,
                                        float highestHeight);

  // "About N chunks, up to M MB" for a flat request, the allowed ceiling for a heightmap (whose size is known once its
  // image is read), and empty when the numbers cannot be sized.
  [[nodiscard]] std::string estimateText(const TerrainCreateRequest& request);

  // The reason a request cannot be created, or nullopt when it can. Checks the name against the registry's terrains
  // and the numbers against the bounds; for a heightmap the image is checked by generateTerrain.
  [[nodiscard]] std::optional<std::string> refusal(const TerrainCreateRequest& request, const AssetRegistry& registry);

  // Decodes an 8- or 16-bit image to 16-bit samples (8-bit scaled by 257). A color image is converted to gray.
  // Refuses an unreadable or unsupported file, one over maxImageFileBytes and one whose header names more than
  // maxSamples samples before decoding it, and one whose body is short.
  [[nodiscard]] HeightmapDecode decodeHeightmap(const std::filesystem::path& file, int64_t maxSamples);

  // The scene edit that places a created terrain: one batch adding a root object named name with the default
  // Transform and a Terrain component that references the terrain.
  [[nodiscard]] nlohmann::json placementEdit(const uuids::uuid& objectUUID, const std::string& name,
                                             const uuids::uuid& terrainUUID);

  // Validates, decodes the heightmap if any and generates the volume. Allocates nothing for a request the bounds
  // refuse.
  [[nodiscard]] TerrainCreateResult generateTerrain(const TerrainCreateRequest& request, const AssetRegistry& registry,
                                                    const uuids::uuid& terrainUUID);
}

// What to do when the form's Create is pressed. A template project is never written, so it asks for Save As first.
enum class TerrainCreateGate { proceed, saveFirst };

class TerrainCreateFlow {
public:
  [[nodiscard]] static TerrainCreateGate gate(const bool writesBlocked)
  {
    return writesBlocked ? TerrainCreateGate::saveFirst : TerrainCreateGate::proceed;
  }

  // Replaces the request with the defaults under a free name and asks for the form to open.
  void begin(const AssetRegistry& registry)
  {
    m_request = {};
    m_request.name = assetNaming::uniqueNameOfType(registry, AssetType::Terrain, "New Terrain");
    m_error.clear();
    m_openRequested = true;
  }

  // True once per begin().
  [[nodiscard]] bool takeOpenRequest()
  {
    return std::exchange(m_openRequested, false);
  }

  [[nodiscard]] TerrainCreateRequest& request()
  {
    return m_request;
  }

  [[nodiscard]] const std::string& error() const
  {
    return m_error;
  }

  void fail(const std::string& message)
  {
    m_error = message;
  }

  void cancel()
  {
    m_request = {};
    m_error.clear();
  }

private:
  TerrainCreateRequest m_request;
  bool m_openRequested = false;
  std::string m_error;
};

#endif //TERRAINCREATEFLOW_H
