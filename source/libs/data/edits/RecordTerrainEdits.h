#ifndef RECORDTERRAINEDITS_H
#define RECORDTERRAINEDITS_H

#include "EditCommand.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainMaterialRemap.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainTypes.h"
#include <Protocol.h>
#include <nlohmann/json_fwd.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <uuid.h>
#include <vector>

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


// What the editor sends and records for a palette removal or move whose painted samples must follow their material.
struct TerrainRemapPrediction {
  // The remapMaterials terrainEdit to send the server.
  net::Message message;
  // The terrainStroke that undoes the remap of the samples; nullopt when the volume held nothing it changed. The
  // palette change is its own replaceAsset command.
  std::optional<EditCommand> command;
};

// Predicts a remap on the editor's own volume, the way the server will apply it, and returns the message and the undo
// command built from the chunks as they were before (commandForTerrainStroke). No hold is needed: the server's echo
// carries the same result, so the sender's hash-only entries match the prediction, and any other outcome (an edit
// another connection made in between) mismatches and is fetched in full. nullopt, with the volume untouched, for an
// invalid or identity remap: there is nothing to send.
[[nodiscard]] std::optional<TerrainRemapPrediction> predictTerrainRemap(const uuids::uuid& terrainUUID,
                                                                       terrain::TerrainVolume& volume,
                                                                       const terrain::MaterialRemap& remap,
                                                                       uint64_t strokeId = 0);

// The command for an addAsset that creates a terrain the store already holds: the description the asset carries (its
// chunk count set to the chunks the store's volume holds), the display name and a snapshot of the chunks. nullopt for
// an unusable description or a terrain the store does not hold. The editor calls it after the new terrain's chunks
// are in its store, since undo of the add removes the asset and redo has to bring the chunks back.
[[nodiscard]] std::optional<EditCommand> commandForTerrainAdd(const nlohmann::json& asset,
                                                              const terrain::TerrainStore& store);

// The command for replacing a terrain's palette: a replaceAsset whose before and after bodies are the description
// bodies (terrain/TerrainAddition.h), so undo and redo send an addAsset the server applies as a palette replacement.
// nullopt for a terrain the store does not hold, a palette that makes the description invalid or one equal to the
// current palette.
[[nodiscard]] std::optional<EditCommand> commandForTerrainPalette(const uuids::uuid& terrainUUID,
                                                                  const terrain::TerrainStore& view,
                                                                  std::vector<terrain::TerrainMaterial> palette);

}

#endif //RECORDTERRAINEDITS_H
