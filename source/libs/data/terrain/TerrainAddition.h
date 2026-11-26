#ifndef TERRAINADDITION_H
#define TERRAINADDITION_H

#include "TerrainStore.h"
#include <nlohmann/json_fwd.hpp>
#include <cstdint>
#include <optional>

namespace terrain
{
  // The most chunks an addAsset may announce for a terrain it creates.
  inline constexpr uint32_t maxAddedTerrainChunks = 1u << 20;

  // The addAsset blob that creates a terrain: its canonical record path, and a body holding the description as JSON
  // (voxelSize, chunkCount, palette), since the wire carries no terrain-specific field.
  [[nodiscard]] nlohmann::json addTerrainAssetJson(const TerrainDescription& description);

  // True when an addAsset blob carries a description body, valid or not.
  [[nodiscard]] bool hasAddedTerrainBody(const nlohmann::json& asset);

  // The description a terrain addAsset carries, with the asset's uuid. Empty for a missing or malformed body, a field
  // of the wrong JSON type, a voxel size outside [minVoxelSize, maxVoxelSize], an invalid description or a chunk count
  // above maxAddedTerrainChunks.
  [[nodiscard]] std::optional<TerrainDescription> parseAddedTerrain(const nlohmann::json& asset);

  // Creates the terrain's empty volume. With expectUpload and a positive chunk count it also records the expectation of
  // an upload, which only the server does: it is the one that receives the chunks. False, with the store untouched,
  // when the store already holds a terrain under the description's uuid.
  bool applyAddedTerrain(TerrainStore& store, const TerrainDescription& description, bool expectUpload);
}

#endif //TERRAINADDITION_H
