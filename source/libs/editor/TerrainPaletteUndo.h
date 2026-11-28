#ifndef TERRAINPALETTEUNDO_H
#define TERRAINPALETTEUNDO_H

#include <Protocol.h>
#include <Replication.h>
#include <edits/EditHistory.h>
#include <terrain/TerrainAddition.h>
#include <terrain/TerrainStore.h>
#include <string>
#include <uuid.h>

// The decision behind applying an undo or redo of a terrain palette edit, with no ImGui.
class TerrainPaletteUndo {
public:
  // Whether an undo or redo payload is a palette replacement of a terrain the store holds, which the editor applies
  // to its own store before sending, so the next undo validates against the palette the server is about to hold.
  // A terrain brought back by undo carries a restore and goes through the restore path instead.
  [[nodiscard]] static bool appliesLocally(const edits::HistoryOutcome& outcome, const terrain::TerrainStore& store)
  {
    if (outcome.terrainRestore || !outcome.messagePayload ||
        outcome.messagePayload->getType() != net::MessageType::addAsset)
    {
      return false;
    }

    const auto asset = replication::unpackAddAsset(*outcome.messagePayload);
    const auto uuid = uuids::uuid::from_string(asset.value("uuid", std::string{}));

    return terrain::hasAddedTerrainBody(asset) && uuid.has_value() && store.description(*uuid) != nullptr;
  }
};

#endif //TERRAINPALETTEUNDO_H
