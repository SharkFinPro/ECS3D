#include "TerrainStreamer.h"
#include "TerrainReplication.h"
#include <exception>
#include <set>
#include <stdexcept>
#include <utility>

namespace terrain
{
  static_assert(streamEntryOverheadBytes == chunkEntryHeaderBytes &&
                streamMessageOverheadBytes == terrainChunksHeaderBytes);

  namespace
  {
    size_t framedSize(const std::vector<StreamedChunk>& batch)
    {
      size_t total = streamMessageOverheadBytes;

      for (const StreamedChunk& streamed : batch)
      {
        total += streamEntryOverheadBytes + streamed.encoded.size();
      }

      return total;
    }

    std::vector<ChunkEntry> makeEntries(TerrainStore& store, const uuids::uuid& terrain,
                                        std::vector<StreamedChunk>& batch)
    {
      std::vector<ChunkEntry> entries;
      entries.reserve(batch.size());

      for (StreamedChunk& streamed : batch)
      {
        uint32_t revision = store.revision(terrain, streamed.coord);

        if (revision == 0)
        {
          revision = store.nextRevision(terrain);
          if (revision == 0 || !store.setChunk(terrain, streamed.coord, streamed.chunk, revision))
          {
            throw std::runtime_error("Could not give a terrain chunk a revision");
          }
        }

        entries.push_back({ streamed.coord, revision, streamed.chunk->hash(), std::move(streamed.encoded) });
      }

      return entries;
    }
  }

  void TerrainStreamer::start(const int32_t connId, const TerrainStore& store, const FocusFor& focusFor)
  {
    Connection connection;

    for (const uuids::uuid& uuid : store.terrains())
    {
      const auto volume = store.find(uuid);
      const TerrainDescription* description = store.description(uuid);

      if (volume && description)
      {
        connection.plans.emplace(uuid, TerrainStreamPlan(*volume, focusFor(uuid, *description)));
      }
    }

    refreshActive(connection);
    m_connections.insert_or_assign(connId, std::move(connection));
  }

  void TerrainStreamer::forget(const int32_t connId)
  {
    m_connections.erase(connId);
    std::erase_if(m_finished, [connId](const StreamFinished& finished) { return finished.connId == connId; });
  }

  void TerrainStreamer::restart(const uuids::uuid& terrain, const TerrainStore& store, const FocusFor& focusFor)
  {
    const auto volume = store.find(terrain);
    const TerrainDescription* description = store.description(terrain);

    for (auto& entry : m_connections)
    {
      Connection& connection = entry.second;
      connection.plans.erase(terrain);
      connection.sent = 0;

      if (volume && description)
      {
        connection.plans.emplace(terrain, TerrainStreamPlan(*volume, focusFor(terrain, *description)));
      }

      refreshActive(connection);
    }
  }

  void TerrainStreamer::requeue(const uuids::uuid& terrain, const std::span<const ChunkCoord> coords)
  {
    for (auto& entry : m_connections)
    {
      Connection& connection = entry.second;
      const auto plan = connection.plans.find(terrain);

      if (plan == connection.plans.end())
      {
        continue;
      }

      for (const ChunkCoord coord : coords)
      {
        plan->second.requeue(coord);
      }

      refreshActive(connection);
    }
  }

  std::vector<StreamFinished> TerrainStreamer::pump(TerrainStore& store, const Send& send,
                                                     const size_t budgetBytesPerConnection)
  {
    std::exception_ptr error;

    for (auto& entry : m_connections)
    {
      Connection& connection = entry.second;
      const bool wasActive = connection.active;

      try
      {
        pumpConnection(entry.first, connection, store, send, budgetBytesPerConnection);
      }
      catch (...)
      {
        if (!error)
        {
          error = std::current_exception();
        }
      }

      refreshActive(connection);

      if (wasActive && !connection.active && connection.sent > 0)
      {
        m_finished.push_back({ entry.first, connection.sent });
      }
    }

    if (error)
    {
      std::rethrow_exception(error);
    }

    return std::exchange(m_finished, {});
  }

  bool TerrainStreamer::streaming(const int32_t connId) const
  {
    const auto found = m_connections.find(connId);
    return found != m_connections.end() && found->second.active;
  }

  size_t TerrainStreamer::pendingChunks(const int32_t connId) const
  {
    const auto found = m_connections.find(connId);
    size_t total = 0;

    if (found != m_connections.end())
    {
      for (const auto& plan : found->second.plans)
      {
        total += plan.second.pending();
      }
    }

    return total;
  }

  void TerrainStreamer::refreshActive(Connection& connection)
  {
    bool pending = false;
    for (const auto& plan : connection.plans)
    {
      pending = pending || !plan.second.done();
    }

    // Work arriving after a stream ended starts the count over, so the finish report describes that run.
    if (pending && !connection.active)
    {
      connection.sent = 0;
    }

    connection.active = pending;
  }

  void TerrainStreamer::pumpConnection(const int32_t connId, Connection& connection, TerrainStore& store,
                                       const Send& send, const size_t budgetBytes)
  {
    size_t remaining = budgetBytes;
    bool first = true;

    for (auto it = connection.plans.begin(); it != connection.plans.end();)
    {
      const auto volume = store.find(it->first);
      if (!volume)
      {
        it = connection.plans.erase(it);
        continue;
      }

      TerrainStreamPlan& plan = it->second;

      while (!plan.done() && (first || remaining > 0))
      {
        // Only the connection's first chunk may exceed the budget; a later one waits for the next call.
        std::vector<StreamedChunk> batch = plan.next(*volume, remaining, maxTerrainChunksEntries, first);
        if (batch.empty())
        {
          if (!plan.done())
          {
            remaining = 0;
          }

          break;
        }

        const size_t framed = framedSize(batch);

        try
        {
          const auto entries = makeEntries(store, it->first, batch);
          send(connId, packTerrainChunks(it->first, entries));
          connection.sent += entries.size();
        }
        catch (...)
        {
          for (const StreamedChunk& streamed : batch)
          {
            plan.requeue(streamed.coord);
          }

          throw;
        }

        first = false;
        remaining = remaining > framed ? remaining - framed : 0;
      }

      ++it;
    }
  }

  VolumeSnapshot captureVolumes(const TerrainStore& store)
  {
    VolumeSnapshot snapshot;

    for (const uuids::uuid& uuid : store.terrains())
    {
      snapshot.emplace(uuid, store.find(uuid));
    }

    return snapshot;
  }

  std::vector<uuids::uuid> replacedVolumes(const VolumeSnapshot& before, const TerrainStore& store)
  {
    std::set<uuids::uuid> replaced;

    for (const uuids::uuid& uuid : store.terrains())
    {
      const auto held = before.find(uuid);
      if (held == before.end() || held->second != store.find(uuid))
      {
        replaced.insert(uuid);
      }
    }

    for (const auto& entry : before)
    {
      if (!store.find(entry.first))
      {
        replaced.insert(entry.first);
      }
    }

    return { replaced.begin(), replaced.end() };
  }
}
