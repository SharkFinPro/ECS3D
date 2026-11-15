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
#include <vector>

class AssetRegistry;

namespace terrain
{
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

  // A region file is named r.<x>.<y>.<z>.<tag>.bin, where the tag hashes the region's chunk set, so a file the current
  // index references is never overwritten. Steps: delete leftover temporary files; write each region file whose name
  // is missing or whose size disagrees with the index (to <name>.tmp, then rename onto the new name); write
  // terrain.json the same way, the only rename over an existing file; then delete region files the new index does not
  // reference. A crash before the index rename leaves the old index with every file it names intact. The description's
  // chunkCount is not stored.
  [[nodiscard]] TerrainSaveResult saveTerrain(const std::filesystem::path& root, const TerrainDescription& description,
                                              const TerrainVolume& volume);

  struct TerrainLoadReport
  {
    size_t loaded = 0;
    // Terrains with no saved folder or index: created but never saved, which is normal.
    size_t missing = 0;
    std::vector<std::string> errors;
  };

  // On failure the error names the offending file, and neither description nor volume is set.
  [[nodiscard]] TerrainLoadResult loadTerrain(const std::filesystem::path& root, const uuids::uuid& uuid);

  // Loads the files of every Terrain record in the registry into the store, in uuid order. A terrain that was never
  // saved, or whose files fail to load, keeps the store's empty terrain. Never throws.
  [[nodiscard]] TerrainLoadReport loadProjectTerrains(const std::filesystem::path& root,
                                                      const AssetRegistry& registry, TerrainStore& store);
}

#endif //TERRAINFILES_H
