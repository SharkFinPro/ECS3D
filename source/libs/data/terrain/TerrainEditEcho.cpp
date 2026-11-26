#include "TerrainEditEcho.h"
#include "TerrainReplication.h"
#include <exception>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace terrain
{
  namespace
  {
    struct ChunkState
    {
      const std::set<int32_t>* senders = nullptr;
      bool shared = false;
      const std::set<int32_t>* requesters = nullptr;
      ChunkEntry full;
    };

    size_t entrySize(const ChunkEntry& entry)
    {
      return chunkEntryHeaderBytes + entry.encoded.size();
    }

    void sendInMessages(const int32_t connId, const uuids::uuid& terrain, const std::vector<ChunkEntry>& entries,
                        const TerrainEditEcho::Send& send, const size_t budgetBytes)
    {
      size_t begin = 0;

      while (begin < entries.size())
      {
        size_t end = begin;
        size_t framed = terrainChunksHeaderBytes;

        while (end < entries.size() && end - begin < maxTerrainChunksEntries &&
               (end == begin || framed + entrySize(entries[end]) <= budgetBytes))
        {
          framed += entrySize(entries[end]);
          ++end;
        }

        send(connId, packTerrainChunks(terrain, std::span<const ChunkEntry>(entries).subspan(begin, end - begin)));
        begin = end;
      }
    }

    uint32_t revisionFor(TerrainStore& store, const uuids::uuid& terrain, const ChunkCoord coord,
                         const std::shared_ptr<const TerrainChunk>& chunk)
    {
      uint32_t revision = store.revision(terrain, coord);

      if (revision == 0)
      {
        revision = store.nextRevision(terrain);
        if (revision == 0 || !store.setChunk(terrain, coord, chunk, revision))
        {
          throw std::runtime_error("Could not give a terrain chunk a revision");
        }
      }

      return revision;
    }
  }

  bool TerrainEditEcho::CoordLess::operator()(const ChunkCoord& a, const ChunkCoord& b) const
  {
    return std::tie(a.z, a.y, a.x) < std::tie(b.z, b.y, b.x);
  }

  void TerrainEditEcho::recordEdit(const uuids::uuid& terrain, const std::span<const ChunkCoord> coords,
                                   const int32_t sender)
  {
    for (const ChunkCoord coord : coords)
    {
      if (isValidChunkCoord(coord))
      {
        EditRecord& record = m_edits[terrain][coord];
        record.shared = record.shared || (!record.senders.empty() && !record.senders.contains(sender));
        record.senders.insert(sender);
      }
    }
  }

  void TerrainEditEcho::requestFull(const uuids::uuid& terrain, const std::span<const ChunkCoord> coords,
                                    const int32_t connId)
  {
    for (const ChunkCoord coord : coords)
    {
      if (isValidChunkCoord(coord))
      {
        m_requests[terrain][coord].insert(connId);
      }
    }
  }

  void TerrainEditEcho::forget(const int32_t connId)
  {
    for (auto terrain = m_requests.begin(); terrain != m_requests.end();)
    {
      for (auto coord = terrain->second.begin(); coord != terrain->second.end();)
      {
        coord->second.erase(connId);
        coord = coord->second.empty() ? terrain->second.erase(coord) : std::next(coord);
      }

      terrain = terrain->second.empty() ? m_requests.erase(terrain) : std::next(terrain);
    }

    for (auto& terrain : m_edits)
    {
      for (auto& coord : terrain.second)
      {
        if (coord.second.senders.erase(connId) > 0)
        {
          coord.second.shared = true;
        }
      }
    }
  }

  void TerrainEditEcho::clear()
  {
    m_edits.clear();
    m_requests.clear();
  }

  bool TerrainEditEcho::pending() const
  {
    return !m_edits.empty() || !m_requests.empty();
  }

  void TerrainEditEcho::flush(TerrainStore& store, const std::span<const int32_t> connections, const Send& send,
                              const size_t budgetBytes)
  {
    const auto edits = std::exchange(m_edits, {});
    const auto requests = std::exchange(m_requests, {});

    std::set<uuids::uuid> terrains;
    for (const auto& entry : edits)
    {
      terrains.insert(entry.first);
    }
    for (const auto& entry : requests)
    {
      terrains.insert(entry.first);
    }

    std::exception_ptr error;

    for (const uuids::uuid& terrain : terrains)
    {
      const auto volume = store.find(terrain);
      if (!volume)
      {
        continue;
      }

      std::map<ChunkCoord, ChunkState, CoordLess> states;

      const auto edited = edits.find(terrain);
      if (edited != edits.end())
      {
        for (const auto& entry : edited->second)
        {
          ChunkState& state = states[entry.first];
          state.senders = &entry.second.senders;
          state.shared = entry.second.shared;
        }
      }

      const auto requested = requests.find(terrain);
      if (requested != requests.end())
      {
        for (const auto& entry : requested->second)
        {
          states[entry.first].requesters = &entry.second;
        }
      }

      try
      {
        for (auto& entry : states)
        {
          const auto chunk = volume->chunk(entry.first);
          entry.second.full = makeChunkEntry(entry.first, revisionFor(store, terrain, entry.first, chunk), chunk);
        }
      }
      catch (...)
      {
        if (!error)
        {
          error = std::current_exception();
        }

        continue;
      }

      for (const int32_t connId : connections)
      {
        std::vector<ChunkEntry> entries;

        for (const auto& entry : states)
        {
          const ChunkState& state = entry.second;
          const bool asked = state.requesters && state.requesters->contains(connId);
          const bool onlySender = state.senders && !state.shared && state.senders->size() == 1 &&
                                  state.senders->contains(connId);

          if (asked || (state.senders && !onlySender))
          {
            entries.push_back(state.full);
          }
          else if (onlySender)
          {
            entries.push_back(makeHashOnlyEntry(state.full.coord, state.full.revision, state.full.hash));
          }
        }

        try
        {
          sendInMessages(connId, terrain, entries, send, budgetBytes);
        }
        catch (...)
        {
          if (!error)
          {
            error = std::current_exception();
          }
        }
      }
    }

    if (error)
    {
      std::rethrow_exception(error);
    }
  }
}
