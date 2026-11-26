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
#include <memory>
#include <span>
#include <uuid.h>
#include <vector>

namespace terrain
{
  // A connection whose stream just ran out of chunks, with how many it was sent since it last became active.
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
    // Must not call back into the streamer.
    using Send = std::function<void(int32_t connId, const net::Message& message)>;

    // Starts a stream of every terrain the store holds to the connection, replacing any stream it already had.
    void start(int32_t connId, const TerrainStore& store, const FocusFor& focusFor);

    void forget(int32_t connId);

    // A terrain's volume was replaced (resetVolumes, adopt, a new voxel size, a project load): restarts that
    // terrain's stream on every connection that has one, from the current volume, and starts every connection's sent
    // count over (not just this terrain's), so a finish report after a restart counts only chunks sent since it. A terrain the store no longer holds is dropped. A restart that leaves nothing to send ends the
    // stream without a finish report: only pump reports, and only streams that sent chunks since they last became
    // active.
    void restart(const uuids::uuid& terrain, const TerrainStore& store, const FocusFor& focusFor);

    // Chunks changed on the server after they may have been sent: requeues them on every connection that has a
    // stream of that terrain.
    void requeue(const uuids::uuid& terrain, std::span<const ChunkCoord> coords);

    // Sends at most budgetBytesPerConnection per connection this call, as terrainChunks messages of at most
    // maxTerrainChunksEntries entries each; a connection's first chunk is sent even when it alone is over the budget,
    // so a stream cannot stall. A chunk whose store revision is 0 is given one before it is sent. Returns the
    // connections whose stream ran out of chunks in this call.
    //
    // Each connection is served on its own: when making or sending a batch throws, that batch's chunks go back on the
    // connection's queue (a later pump sends them again), the other connections are still served, and the first
    // exception is rethrown once every connection has had its turn so the caller can log it. Finish reports gathered
    // by a pump that rethrows are returned by the next pump.
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
    std::vector<StreamFinished> m_finished;
  };

  using VolumeSnapshot = std::map<uuids::uuid, std::shared_ptr<const TerrainVolume>>;

  // The volume each terrain in the store holds right now, to compare against after something that may replace them.
  [[nodiscard]] VolumeSnapshot captureVolumes(const TerrainStore& store);

  // The terrains added, removed or whose volume object changed between before and the store now, in uuid order: the
  // ones whose streams need restarting.
  [[nodiscard]] std::vector<uuids::uuid> replacedVolumes(const VolumeSnapshot& before, const TerrainStore& store);
}

#endif //TERRAINSTREAMER_H
