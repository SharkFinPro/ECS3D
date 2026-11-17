#ifndef TERRAINUPLOAD_H
#define TERRAINUPLOAD_H

#include "TerrainStreamPlan.h"
#include "TerrainVolume.h"
#include <Protocol.h>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <uuid.h>

namespace terrain
{
  // Sends the chunks of held volumes to the server as terrainChunks messages. Holds the volumes, not a store, because
  // the editor's store is reset when its own project load snapshot comes back. Not thread safe.
  class TerrainUpload
  {
  public:
    // Queues every chunk the volume holds, replacing an earlier volume added for the same terrain. The order is by
    // distance from the volume's sample-space origin: an upload has no viewer, so any deterministic order will do.
    void add(const uuids::uuid& terrain, std::shared_ptr<const TerrainVolume> volume);

    void clear();

    // Sends at most budgetBytes this call (the first chunk always goes), at most maxTerrainChunksEntries per message,
    // terrains in uuid order. Every entry carries revision 1: the server assigns its own revisions to an upload. A
    // batch whose send throws goes back on its queue and the exception is rethrown. Returns the chunks sent.
    size_t pump(const std::function<void(const net::Message&)>& send,
                size_t budgetBytes = defaultStreamBudgetBytes);

    [[nodiscard]] bool done() const;

    [[nodiscard]] size_t pendingChunks() const;

  private:
    struct Item
    {
      std::shared_ptr<const TerrainVolume> volume;
      TerrainStreamPlan plan;
    };

    std::map<uuids::uuid, Item> m_items;
  };
}

#endif //TERRAINUPLOAD_H
