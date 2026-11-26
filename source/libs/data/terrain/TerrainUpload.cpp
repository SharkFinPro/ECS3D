#include "TerrainUpload.h"
#include "TerrainReplication.h"
#include "TerrainStore.h"
#include <utility>
#include <vector>

namespace terrain
{
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

    // Takes the encoded bytes out of the batch, so a caller that needs the batch afterward reads only its coordinates.
    std::vector<ChunkEntry> makeEntries(std::vector<StreamedChunk>& batch)
    {
      std::vector<ChunkEntry> entries;
      entries.reserve(batch.size());

      for (StreamedChunk& streamed : batch)
      {
        entries.push_back({ streamed.coord, 1, streamed.chunk->hash(), std::move(streamed.encoded) });
      }

      return entries;
    }
  }

  void TerrainUpload::add(const uuids::uuid& terrain, std::shared_ptr<const TerrainVolume> volume)
  {
    if (volume == nullptr)
    {
      return;
    }

    TerrainStreamPlan plan(*volume, glm::vec3(0.0f));
    m_items.insert_or_assign(terrain, Item{ std::move(volume), std::move(plan) });
  }

  void TerrainUpload::clear()
  {
    m_items.clear();
  }

  bool TerrainUpload::forget(const uuids::uuid& terrain)
  {
    return m_items.erase(terrain) > 0;
  }

  std::vector<uuids::uuid> TerrainUpload::terrains() const
  {
    std::vector<uuids::uuid> result;
    result.reserve(m_items.size());

    for (const auto& entry : m_items)
    {
      result.push_back(entry.first);
    }

    return result;
  }

  std::vector<uuids::uuid> forgetUnknownTerrains(TerrainUpload& upload, const TerrainStore& store,
                                                 const std::function<bool(const uuids::uuid&)>& isRecent)
  {
    std::vector<uuids::uuid> forgotten;

    for (const uuids::uuid& terrain : upload.terrains())
    {
      if (store.find(terrain) == nullptr && !(isRecent && isRecent(terrain)) && upload.forget(terrain))
      {
        forgotten.push_back(terrain);
      }
    }

    return forgotten;
  }

  size_t TerrainUpload::pump(const std::function<void(const net::Message&)>& send, const size_t budgetBytes)
  {
    size_t sent = 0;
    size_t remaining = budgetBytes;
    bool first = true;

    for (auto& [terrain, item] : m_items)
    {
      TerrainStreamPlan& plan = item.plan;

      while (!plan.done() && (first || remaining > 0))
      {
        std::vector<StreamedChunk> batch = plan.next(*item.volume, remaining, maxTerrainChunksEntries, first);
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
          const auto entries = makeEntries(batch);
          send(packTerrainChunks(terrain, entries));
        }
        catch (...)
        {
          for (const StreamedChunk& streamed : batch)
          {
            plan.requeue(streamed.coord);
          }

          throw;
        }

        sent += batch.size();
        first = false;
        remaining = remaining > framed ? remaining - framed : 0;
      }
    }

    std::erase_if(m_items, [](const auto& entry) { return entry.second.plan.done(); });

    return sent;
  }

  bool TerrainUpload::done() const
  {
    return pendingChunks() == 0;
  }

  size_t TerrainUpload::pendingChunks() const
  {
    size_t total = 0;

    for (const auto& entry : m_items)
    {
      total += entry.second.plan.pending();
    }

    return total;
  }
}
