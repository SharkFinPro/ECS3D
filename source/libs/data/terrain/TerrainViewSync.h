#ifndef TERRAINVIEWSYNC_H
#define TERRAINVIEWSYNC_H

#include "TerrainReplication.h"
#include <Protocol.h>
#include <LogEntry.h>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <uuid.h>
#include <vector>

class SceneManager;

namespace terrain
{
  struct ViewLogLine
  {
    LogLevel level = LogLevel::info;
    std::string text;
  };

  struct ViewApplyOutcome
  {
    // The message was not a well-formed terrainChunks payload; the store was not touched.
    bool malformed = false;
    uuids::uuid terrain;
    ChunksApplyResult result;
    // Set when hash-only entries did not match what the store holds: the terrainChunks message to send the server
    // to ask for those chunks in full.
    std::optional<net::Message> request;
    std::vector<ViewLogLine> log;
  };

  // What a client or editor keeps to follow the terrain a server streams to it. Not thread safe.
  class TerrainViewSync
  {
  public:
    // Called with each snapshot's project load id after it is unpacked. A load id different from the previous
    // snapshot's empties every volume (and with it the revisions, so the new project's chunks apply) and restarts the
    // completion reports. The first snapshot is not a change. True when it reset.
    bool observeSnapshot(TerrainStore& store, const uuids::uuid& loadId);

    // Forgets the previous load id and every completion report, for a view that reconnects: the next snapshot is a
    // first snapshot again.
    void reset();

    // Applies a terrainChunks message into the store and invalidates the bounds of the Terrain components bound to
    // that terrain. Hash-only entries the store could not satisfy come back as outcome.request for the caller to send
    // to the server. The log lines are for the caller to write: a refused message, an unknown terrain, that request
    // and a terrain that now holds as many chunks as its description's chunkCount, once until it falls below that
    // count again.
    ViewApplyOutcome apply(TerrainStore& store, const SceneManager& scenes, const net::Message& message);

  private:
    void forgetCompletionIfIncomplete(const TerrainStore& store, const uuids::uuid& terrain);

    std::optional<uuids::uuid> m_loadId;
    std::set<uuids::uuid> m_completed;
  };
}

#endif //TERRAINVIEWSYNC_H
