#include "TerrainRestore.h"
#include "Replication.h"
#include "assets/AssetRegistry.h"
#include "terrain/TerrainAddition.h"
#include "terrain/TerrainUpload.h"
#include <Log.h>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <exception>
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

namespace {
  void rollbackTerrain(AssetRegistry& assetRegistry, terrain::TerrainStore& store, terrain::TerrainUpload& upload,
                       const uuids::uuid& uuid, const bool hadRecord)
  {
    upload.forget(uuid);
    store.remove(uuid);

    if (!hadRecord && assetRegistry.getByUUIDOfType(uuid, AssetType::Terrain))
    {
      assetRegistry.removeAsset(uuid);
    }
  }
}

TerrainRestoreResult restoreTerrainAndSend(
  AssetRegistry& assetRegistry, SceneManager& sceneManager, const std::shared_ptr<ComponentRegistry>& componentRegistry,
  terrain::TerrainStore& store, terrain::TerrainUpload& upload, const TerrainRestore& restore,
  const std::function<void()>& send)
{
  const uuids::uuid& uuid = restore.description.uuid;
  std::shared_ptr<const terrain::TerrainVolume> volume;
  bool sent = false;

  // An add over a held uuid is refused before any change, so a rollback only follows an add that started here.
  const bool heldBefore = store.find(uuid) != nullptr;
  const bool hadRecord = assetRegistry.getByUUID(uuid) != nullptr;

  try
  {
    volume = applyTerrainRestore(assetRegistry, sceneManager, componentRegistry, store, restore);
    if (!volume)
    {
      return TerrainRestoreResult::refused;
    }

    send();
    sent = true;

    if (!restore.chunks.empty())
    {
      upload.add(uuid, volume);
    }

    return TerrainRestoreResult::applied;
  }
  catch (const std::exception& e)
  {
    Log::error(LogCategory::editor, "Restoring terrain " + uuids::to_string(uuid) + " failed (" + e.what() +
                                      "); rolled back.");
  }
  catch (...)
  {
    Log::error(LogCategory::editor, "Restoring terrain " + uuids::to_string(uuid) + " failed; rolled back.");
  }

  if (!heldBefore)
  {
    rollbackTerrain(assetRegistry, store, upload, uuid, hadRecord);
  }

  return sent ? TerrainRestoreResult::failedAfterSend : TerrainRestoreResult::failed;
}

}
