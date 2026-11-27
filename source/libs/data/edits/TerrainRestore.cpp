#include "TerrainRestore.h"
#include "Replication.h"
#include "terrain/TerrainAddition.h"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace edits {

TerrainRestore makeTerrainRestore(terrain::TerrainDescription description, terrain::TerrainVolume::ChunkMap chunks,
                                  std::string displayName)
{
  description.chunkCount = static_cast<uint32_t>(chunks.size());

  return { std::move(description), std::move(chunks), std::move(displayName) };
}

nlohmann::json terrainRestoreAssetJson(const TerrainRestore& restore)
{
  auto asset = terrain::addTerrainAssetJson(restore.description);

  if (!restore.displayName.empty())
  {
    asset["displayName"] = restore.displayName;
  }

  return asset;
}

std::shared_ptr<const terrain::TerrainVolume> applyTerrainRestore(
  AssetRegistry& assetRegistry, SceneManager& sceneManager, const std::shared_ptr<ComponentRegistry>& componentRegistry,
  terrain::TerrainStore& store, const TerrainRestore& restore)
{
  if (!replication::applyAddAssetWithTerrain(assetRegistry, sceneManager, componentRegistry, store,
                                             terrainRestoreAssetJson(restore), false))
  {
    return nullptr;
  }

  const auto volume = store.find(restore.description.uuid);
  if (!volume)
  {
    return nullptr;
  }

  volume->restoreChunks(restore.chunks);

  auto upload = std::make_shared<terrain::TerrainVolume>(restore.description.voxelSize);
  upload->restoreChunks(restore.chunks);

  return upload;
}

}
