#ifndef RECORDTERRAINEDITS_H
#define RECORDTERRAINEDITS_H

#include "EditCommand.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainTypes.h"
#include <nlohmann/json_fwd.hpp>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <uuid.h>

namespace terrain {
  class TerrainStore;
  class TerrainVolume;
}

namespace edits {

// The command for a finished sculpt stroke. before is each touched chunk's pointer from before the stroke (null =
// air, as TerrainStroke::Finished::before holds it) and the after chunk is read from the volume now. A chunk whose
// content did not change is dropped, and a repeated coordinate keeps its first entry; nullopt when none changed, since a stroke that changed nothing records nothing.
[[nodiscard]] std::optional<EditCommand> commandForTerrainStroke(
  const uuids::uuid& terrainUUID,
  std::span<const std::pair<terrain::ChunkCoord, std::shared_ptr<const terrain::TerrainChunk>>> before,
  const terrain::TerrainVolume& volume);


// The command for an addAsset that creates a terrain the store already holds: the description the asset carries (its
// chunk count set to the chunks the store's volume holds), the display name and a snapshot of the chunks. nullopt for
// an unusable description or a terrain the store does not hold. The editor calls it after the new terrain's chunks
// are in its store, since undo of the add removes the asset and redo has to bring the chunks back.
[[nodiscard]] std::optional<EditCommand> commandForTerrainAdd(const nlohmann::json& asset,
                                                              const terrain::TerrainStore& store);

}

#endif //RECORDTERRAINEDITS_H
