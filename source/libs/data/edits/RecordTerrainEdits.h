#ifndef RECORDTERRAINEDITS_H
#define RECORDTERRAINEDITS_H

#include "EditCommand.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainTypes.h"
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <uuid.h>

namespace terrain {
  class TerrainVolume;
}

namespace edits {

// The command for a finished sculpt stroke. before is each touched chunk's pointer from before the stroke (null =
// air, as TerrainStroke::Finished::before holds it) and the after chunk is read from the volume now. A chunk whose
// content did not change is dropped; nullopt when none changed, since a stroke that changed nothing records nothing.
[[nodiscard]] std::optional<EditCommand> commandForTerrainStroke(
  const uuids::uuid& terrainUUID,
  std::span<const std::pair<terrain::ChunkCoord, std::shared_ptr<const terrain::TerrainChunk>>> before,
  const terrain::TerrainVolume& volume);

}

#endif //RECORDTERRAINEDITS_H
