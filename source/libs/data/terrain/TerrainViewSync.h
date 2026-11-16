#ifndef TERRAINVIEWSYNC_H
#define TERRAINVIEWSYNC_H

#include "TerrainReplication.h"
#include <Protocol.h>
#include <LogEntry.h>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>
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
    std::vector<ViewLogLine> log;
  };

  // What a client or editor keeps to follow the terrain a server streams to it. Not thread safe.
  class TerrainViewSync
  {
  public:
    // Called with each snapshot's project load id and fingerprint after it is unpacked. A project different from the
    // previous snapshot's empties every volume (and with it the revisions, so the new project's chunks apply) and
    // restarts the completion reports. The first snapshot is not a change. True when it reset.
    bool observeSnapshot(TerrainStore& store, const uuids::uuid& loadId, uint64_t fingerprint);

    // Applies a terrainChunks message into the store and invalidates the bounds of the Terrain components bound to
    // that terrain. The log lines are for the caller to write: a refused message, entries the stream never sends
    // (an unknown terrain, hash-only entries the store could not satisfy) and, once per project, a terrain that now
    // holds as many chunks as its description's chunkCount.
    ViewApplyOutcome apply(TerrainStore& store, const SceneManager& scenes, const net::Message& message);

  private:
    std::optional<std::pair<uuids::uuid, uint64_t>> m_project;
    std::set<uuids::uuid> m_completed;
  };
}

#endif //TERRAINVIEWSYNC_H
