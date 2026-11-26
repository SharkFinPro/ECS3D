#include "TerrainEdit.h"
#include "TerrainCodec.h"
#include "TerrainReplication.h"
#include "WireTypes.h"
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace terrain
{
  namespace
  {
    constexpr size_t editHeaderBytes = sizeof(uuids::uuid) + sizeof(uint64_t) + sizeof(uint8_t);

    // kind, center, radius, strength, falloff, material, plane point, plane normal.
    constexpr size_t brushBytes = sizeof(uint8_t) + 3 * sizeof(float) + 3 * sizeof(float) + sizeof(uint8_t) +
                                  3 * sizeof(float) + 3 * sizeof(float);

    constexpr size_t boxBytes = 6 * sizeof(int32_t);

    std::shared_ptr<const TerrainChunk> airChunk()
    {
      static const auto air = TerrainChunk::uniform(airDistance, 0);
      return air;
    }

    net::Message startEdit(const uuids::uuid& terrain, const uint64_t strokeId, const TerrainEditKind kind,
                           const size_t bodyBytes)
    {
      net::Message message(net::MessageType::terrainEdit);
      message.reserve(editHeaderBytes + bodyBytes);
      message.write(terrain);
      message.write(strokeId);
      message.write(static_cast<uint8_t>(kind));
      return message;
    }

    std::optional<SampleBox> unpackBox(net::MessageReader& reader)
    {
      SampleBox box;
      box.min.x = reader.read<int32_t>();
      box.min.y = reader.read<int32_t>();
      box.min.z = reader.read<int32_t>();
      box.max.x = reader.read<int32_t>();
      box.max.y = reader.read<int32_t>();
      box.max.z = reader.read<int32_t>();

      return box.empty() ? std::nullopt : std::optional<SampleBox>(box);
    }

    bool unpackSetChunks(net::MessageReader& reader, TerrainEditPayload& payload)
    {
      const auto count = reader.read<uint32_t>();

      if (count == 0 || count > maxTerrainChunksEntries || count > reader.remaining() / setChunkEntryHeaderBytes)
      {
        return false;
      }

      payload.chunks.reserve(count);
      std::unordered_set<ChunkCoord> seen;
      seen.reserve(count);

      for (uint32_t i = 0; i < count; ++i)
      {
        SetChunkEntry entry;
        entry.coord.x = reader.read<int32_t>();
        entry.coord.y = reader.read<int32_t>();
        entry.coord.z = reader.read<int32_t>();
        const auto hash = reader.read<uint64_t>();
        const auto size = reader.read<uint32_t>();

        if (!isValidChunkCoord(entry.coord) || size > maxEncodedChunkBytes || !seen.insert(entry.coord).second)
        {
          return false;
        }

        entry.chunk = decodeChunk(reader.readBytes(size));
        if (!entry.chunk || entry.chunk->hash() != hash)
        {
          return false;
        }

        payload.chunks.push_back(std::move(entry));
      }

      return true;
    }
  }

  net::Message packBrushEdit(const uuids::uuid& terrain, const uint64_t strokeId, const BrushOp& op)
  {
    net::Message message = startEdit(terrain, strokeId, TerrainEditKind::brush, brushBytes);
    pack(message, op);
    return message;
  }

  net::Message packRedistanceEdit(const uuids::uuid& terrain, const uint64_t strokeId, const SampleBox& box)
  {
    net::Message message = startEdit(terrain, strokeId, TerrainEditKind::redistance, boxBytes);
    message.write(box.min.x).write(box.min.y).write(box.min.z);
    message.write(box.max.x).write(box.max.y).write(box.max.z);
    return message;
  }

  net::Message packSetChunksEdit(const uuids::uuid& terrain, const uint64_t strokeId,
                                 const std::span<const SetChunkEntry> chunks)
  {
    if (chunks.size() > maxTerrainChunksEntries)
    {
      throw std::length_error("Too many terrain chunk entries for one message");
    }

    std::vector<std::vector<uint8_t>> encoded;
    encoded.reserve(chunks.size());
    size_t bytes = sizeof(uint32_t);

    for (const SetChunkEntry& entry : chunks)
    {
      encoded.push_back(encodeChunk(entry.chunk ? *entry.chunk : *airChunk()));
      bytes += setChunkEntryHeaderBytes + encoded.back().size();
    }

    net::Message message = startEdit(terrain, strokeId, TerrainEditKind::setChunks, bytes);
    message.write(static_cast<uint32_t>(chunks.size()));

    for (size_t i = 0; i < chunks.size(); ++i)
    {
      const SetChunkEntry& entry = chunks[i];
      const auto& sent = entry.chunk ? entry.chunk : airChunk();

      message.write(entry.coord.x).write(entry.coord.y).write(entry.coord.z);
      message.write(sent->hash());
      message.write(static_cast<uint32_t>(encoded[i].size()));
      message.appendBytes(encoded[i]);
    }

    return message;
  }

  std::optional<TerrainEditPayload> unpackTerrainEdit(const net::Message& message)
  {
    if (message.getType() != net::MessageType::terrainEdit)
    {
      return std::nullopt;
    }

    try
    {
      net::MessageReader reader(message);

      TerrainEditPayload payload;
      payload.terrain = reader.read<uuids::uuid>();
      payload.strokeId = reader.read<uint64_t>();
      const auto kind = reader.read<uint8_t>();

      if (payload.terrain.is_nil() || kind > static_cast<uint8_t>(TerrainEditKind::setChunks))
      {
        return std::nullopt;
      }

      payload.kind = static_cast<TerrainEditKind>(kind);

      switch (payload.kind)
      {
        case TerrainEditKind::brush:
        {
          const auto brush = unpackBrushOp(reader);
          if (!brush)
          {
            return std::nullopt;
          }

          payload.brush = *brush;
          break;
        }

        case TerrainEditKind::redistance:
        {
          const auto box = unpackBox(reader);
          if (!box)
          {
            return std::nullopt;
          }

          payload.box = *box;
          break;
        }

        case TerrainEditKind::setChunks:
          if (!unpackSetChunks(reader, payload))
          {
            return std::nullopt;
          }

          break;
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
}
