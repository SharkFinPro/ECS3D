#include "TerrainReplication.h"
#include "TerrainCodec.h"
#include "WireTypes.h"
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace terrain
{
  namespace
  {
    std::shared_ptr<const TerrainChunk> airChunk()
    {
      static const auto air = TerrainChunk::uniform(airDistance, 0);
      return air;
    }
  }

  ChunkEntry makeChunkEntry(const ChunkCoord coord, const uint32_t revision,
                            const std::shared_ptr<const TerrainChunk>& chunk)
  {
    const auto sent = chunk ? chunk : airChunk();

    return { coord, revision, sent->hash(), encodeChunk(*sent) };
  }

  ChunkEntry makeHashOnlyEntry(const ChunkCoord coord, const uint32_t revision, const uint64_t hash)
  {
    return { coord, revision, hash, {} };
  }

  net::Message packTerrainChunks(const uuids::uuid& terrain, const std::span<const ChunkEntry> entries)
  {
    if (entries.size() > maxTerrainChunksEntries)
    {
      throw std::length_error("Too many terrain chunk entries for one message");
    }

    size_t bytes = terrainChunksHeaderBytes;
    for (const ChunkEntry& entry : entries)
    {
      bytes += chunkEntryHeaderBytes + entry.encoded.size();
    }

    net::Message message(net::MessageType::terrainChunks);
    message.reserve(bytes);
    message.write(terrain);
    message.write(static_cast<uint32_t>(entries.size()));

    for (const ChunkEntry& entry : entries)
    {
      message.write(entry.coord.x).write(entry.coord.y).write(entry.coord.z);
      message.write(entry.revision);
      message.write(entry.hash);
      message.write(static_cast<uint32_t>(entry.encoded.size()));
      message.appendBytes(entry.encoded);
    }

    return message;
  }

  std::optional<TerrainChunksPayload> unpackTerrainChunks(const net::Message& message)
  {
    if (message.getType() != net::MessageType::terrainChunks)
    {
      return std::nullopt;
    }

    try
    {
      net::MessageReader reader(message);

      TerrainChunksPayload payload;
      payload.terrain = reader.read<uuids::uuid>();
      const auto count = reader.read<uint32_t>();

      if (payload.terrain.is_nil() || count > maxTerrainChunksEntries ||
          count > reader.remaining() / chunkEntryHeaderBytes)
      {
        return std::nullopt;
      }

      payload.entries.reserve(count);
      std::unordered_set<ChunkCoord> seen;
      seen.reserve(count);

      for (uint32_t i = 0; i < count; ++i)
      {
        ReceivedChunk received;
        received.coord.x = reader.read<int32_t>();
        received.coord.y = reader.read<int32_t>();
        received.coord.z = reader.read<int32_t>();
        received.revision = reader.read<uint32_t>();
        received.hash = reader.read<uint64_t>();
        const auto size = reader.read<uint32_t>();

        if (!isValidChunkCoord(received.coord) || received.revision == 0 || size > maxEncodedChunkBytes ||
            !seen.insert(received.coord).second)
        {
          return std::nullopt;
        }

        if (size > 0)
        {
          received.chunk = decodeChunk(reader.readBytes(size));
          if (!received.chunk || received.chunk->hash() != received.hash)
          {
            return std::nullopt;
          }
        }

        payload.entries.push_back(std::move(received));
      }

      if (reader.remaining() != 0)
      {
        return std::nullopt;
      }

      return payload;
    }
    catch (const std::runtime_error&)
    {
      return std::nullopt;
    }
  }

  ChunksApplyResult applyTerrainChunks(TerrainStore& store, const TerrainChunksPayload& payload)
  {
    ChunksApplyResult result;

    const auto volume = store.find(payload.terrain);
    if (volume == nullptr)
    {
      result.unknownTerrain = true;
      return result;
    }

    for (const ReceivedChunk& entry : payload.entries)
    {
      if (entry.revision <= store.revision(payload.terrain, entry.coord))
      {
        ++result.stale;
        continue;
      }

      if (entry.chunk)
      {
        if (store.setChunk(payload.terrain, entry.coord, entry.chunk, entry.revision))
        {
          ++result.applied;
        }

        continue;
      }

      const auto held = volume->chunk(entry.coord);
      const uint64_t heldHash = held ? held->hash() : airChunk()->hash();

      if (heldHash == entry.hash && store.setChunk(payload.terrain, entry.coord, held, entry.revision))
      {
        ++result.applied;
      }
      else
      {
        result.missing.push_back(entry.coord);
      }
    }

    return result;
  }
}
