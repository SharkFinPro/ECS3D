#ifndef TERRAINSTREAMER_H
#define TERRAINSTREAMER_H

#include "TerrainStore.h"
#include "TerrainStreamPlan.h"
#include "TerrainTypes.h"
#include <Protocol.h>
#include <glm/vec3.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <uuid.h>
#include <vector>

namespace terrain
{
  // A connection whose stream just ran out of chunks, with how many it was sent since it last started.
  struct StreamFinished
  {
    int32_t connId = 0;
    size_t chunks = 0;
  };

  // Not thread safe; the server drives it from its tick thread.
  class TerrainStreamer
  {
  public:
    using FocusFor = std::function<glm::vec3(const uuids::uuid& terrain, const TerrainDescription& description)>;
    using Send = std::function<void(int32_t connId, const net::Message& message)>;

    // Starts a stream of every terrain the store holds to the connection, replacing any stream it already had.
    void start(int32_t connId, const TerrainStore& store, const FocusFor& focusFor);

    void forget(int32_t connId);

    // A terrain's volume was replaced (resetVolumes, adopt, a new voxel size, a project load): restarts that
    // terrain's stream on every connection that has one, from the current volume. A terrain the store no longer holds
    // is dropped.
    void restart(const uuids::uuid& terrain, const TerrainStore& store, const FocusFor& focusFor);

    // Chunks changed on the server after they may have been sent: requeues them on every connection that has a
    // stream of that terrain.
    void requeue(const uuids::uuid& terrain, std::span<const ChunkCoord> coords);

    // Sends at most budgetBytesPerConnection per connection this call, as terrainChunks messages of at most
    // maxTerrainChunksEntries entries each; a connection's first chunk is sent even when it alone is over the budget,
    // so a stream cannot stall. A chunk whose store revision is 0 is given one before it is sent. Returns the
    // connections whose stream ran out of chunks in this call.
    std::vector<StreamFinished> pump(TerrainStore& store, const Send& send,
                                     size_t budgetBytesPerConnection = defaultStreamBudgetBytes);

    [[nodiscard]] bool streaming(int32_t connId) const;

    [[nodiscard]] size_t pendingChunks(int32_t connId) const;

  private:
    struct Connection
    {
      // Ordered by uuid so a connection's terrains are served in a deterministic order.
      std::map<uuids::uuid, TerrainStreamPlan> plans;
      size_t sent = 0;
      bool active = false;
    };

    static void refreshActive(Connection& connection);

    // Sends what one connection's budget allows, serving its terrains in uuid order.
    static void pumpConnection(int32_t connId, Connection& connection, TerrainStore& store, const Send& send,
                               size_t budgetBytes);

    std::map<int32_t, Connection> m_connections;
  };
}

#endif //TERRAINSTREAMER_H
