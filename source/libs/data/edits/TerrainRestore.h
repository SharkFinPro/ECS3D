#ifndef TERRAINRESTORE_H
#define TERRAINRESTORE_H

#include "terrain/TerrainStore.h"
#include "terrain/TerrainVolume.h"
#include <nlohmann/json_fwd.hpp>
#include <functional>
#include <memory>
#include <string>

class AssetRegistry;
class ComponentRegistry;
class SceneManager;

namespace terrain {
  class TerrainUpload;
}

namespace edits {

// A terrain a history entry can bring back: its description, every chunk it held (chunks are immutable, so the
// pointers are the recorded state) and the display name of its asset record.
struct TerrainRestore {
  terrain::TerrainDescription description;
  terrain::TerrainVolume::ChunkMap chunks;
  std::string displayName;

  friend bool operator==(const TerrainRestore&, const TerrainRestore&) = default;
};

// The description's chunk count is set to the chunks held: the server waits for exactly that many after the addAsset,
// and a count left over from before a sculpt would hold Play forever.
[[nodiscard]] TerrainRestore makeTerrainRestore(terrain::TerrainDescription description,
                                                terrain::TerrainVolume::ChunkMap chunks, std::string displayName);

// The addAsset blob that creates the terrain again (terrain::addTerrainAssetJson plus the display name).
[[nodiscard]] nlohmann::json terrainRestoreAssetJson(const TerrainRestore& restore);

// Applies the terrain's addAsset to the registry and store as the editor does for a create, then installs the chunks in
// the store's new volume. Returns a volume holding the same chunks for an upload, or null, with nothing changed, when
// the add is refused (the store or registry already holds the uuid).
[[nodiscard]] std::shared_ptr<const terrain::TerrainVolume> applyTerrainRestore(
  AssetRegistry& assetRegistry, SceneManager& sceneManager, const std::shared_ptr<ComponentRegistry>& componentRegistry,
  terrain::TerrainStore& store, const TerrainRestore& restore);

enum class TerrainRestoreResult {
  applied,
  // The registry or store already holds the uuid: nothing changed.
  refused,
  // Something threw before send completed: the registry, store and queued upload are back as they were.
  failed,
  // send completed and queueing the chunks then threw: local state is rolled back, but the server has the addAsset.
  failedAfterSend
};

// Brings a terrain back in the order that keeps the editor and the server consistent: apply to the local registry and
// store, call send (the addAsset), then queue the chunks on upload, so the server never expects chunks the editor
// cannot send. Any throw rolls the local state back and is logged. A caller that gets failedAfterSend sends the
// removeAsset.
[[nodiscard]] TerrainRestoreResult restoreTerrainAndSend(
  AssetRegistry& assetRegistry, SceneManager& sceneManager, const std::shared_ptr<ComponentRegistry>& componentRegistry,
  terrain::TerrainStore& store, terrain::TerrainUpload& upload, const TerrainRestore& restore,
  const std::function<void()>& send);

}

#endif //TERRAINRESTORE_H
