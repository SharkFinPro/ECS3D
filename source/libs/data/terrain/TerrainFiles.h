#ifndef TERRAINFILES_H
#define TERRAINFILES_H

#include "TerrainStore.h"
#include "TerrainVolume.h"
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <uuid.h>

namespace terrain
{
  // Chunks per region edge: a region file holds the chunks of one regionSize^3 block of chunk coordinates.
  inline constexpr int regionSize = 8;

  inline constexpr int terrainFormatVersion = 1;

  // <root>/terrain/<lowercase uuid>
  [[nodiscard]] std::filesystem::path terrainFolder(const std::filesystem::path& root, const uuids::uuid& uuid);

  struct TerrainSaveResult
  {
    bool ok = false;
    std::string error;
    size_t regionsWritten = 0;
    size_t regionsRemoved = 0;
  };

  struct TerrainLoadResult
  {
    std::optional<TerrainDescription> description;
    std::shared_ptr<TerrainVolume> volume;
    std::string error;
  };

  // Rewrites only the region files whose chunks changed, then the index, so a crash mid-save leaves the old index
  // pointing at region files that still match it. Obsolete region files go last. The description's chunkCount is
  // not stored.
  [[nodiscard]] TerrainSaveResult saveTerrain(const std::filesystem::path& root, const TerrainDescription& description,
                                              const TerrainVolume& volume);

  // On failure the error names the offending file, and neither description nor volume is set.
  [[nodiscard]] TerrainLoadResult loadTerrain(const std::filesystem::path& root, const uuids::uuid& uuid);
}

#endif //TERRAINFILES_H
