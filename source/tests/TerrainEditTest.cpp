#include <gtest/gtest.h>

#include "WireTypes.h"
#include "terrain/TerrainBrush.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainCodec.h"
#include "terrain/TerrainEdit.h"
#include "terrain/TerrainReplication.h"
#include "terrain/TerrainTypes.h"

#include <Protocol.h>
#include <glm/vec3.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
  const auto terrainUUID = uuids::uuid::from_string("22222222-2222-2222-2222-222222222222").value();

  constexpr uint64_t strokeId = 0x0102030405060708ull;

  // uuid, stroke id, kind byte.
  constexpr size_t headerBytes = 16 + 8 + 1;
  // header, count, then the first entry's x, y, z.
  constexpr size_t firstHashOffset = headerBytes + 4 + 12;

  std::shared_ptr<const terrain::TerrainChunk> denseChunk()
  {
    std::vector<int16_t> distances(terrain::samplesPerChunk);
    std::vector<uint8_t> materials(terrain::samplesPerChunk, 1);

    for (int z = 0; z < terrain::chunkSize; ++z)
    {
      for (int y = 0; y < terrain::chunkSize; ++y)
      {
        for (int x = 0; x < terrain::chunkSize; ++x)
        {
          distances[static_cast<size_t>(terrain::sampleIndex(x, y, z))] =
            terrain::encodeDistance(static_cast<float>(y) - 16.3f + 0.01f * static_cast<float>(x));
        }
      }
    }

    return terrain::TerrainChunk::dense(std::move(distances), std::move(materials));
  }

  std::shared_ptr<const terrain::TerrainChunk> solidChunk()
  {
    return terrain::TerrainChunk::uniform(-terrain::airDistance, 2);
  }

  terrain::BrushOp sampleBrush()
  {
    terrain::BrushOp op;
    op.kind = terrain::BrushOp::Kind::flatten;
    op.center = { 80.0f, 12.0f, -40.5f };
    op.radius = 20.0f;
    op.strength = 0.8f;
    op.falloff = 0.5f;
    op.material = 7;
    op.planePoint = { 81.0f, 14.0f, 82.0f };
    op.planeNormal = { 0.1f, 1.0f, 0.0f };
    return op;
  }

  terrain::SampleBox sampleBox()
  {
    return { { -3, 4, 5 }, { 10, 20, 30 } };
  }

  struct RawEntry
  {
    terrain::ChunkCoord coord;
    uint64_t hash = 0;
    uint32_t size = 0;
    std::vector<uint8_t> bytes;
  };

  RawEntry rawFor(const terrain::ChunkCoord coord, const std::shared_ptr<const terrain::TerrainChunk>& chunk)
  {
    auto bytes = terrain::encodeChunk(*chunk);
    return { coord, chunk->hash(), static_cast<uint32_t>(bytes.size()), std::move(bytes) };
  }

  net::Message rawSetChunks(const uint32_t count, const std::vector<RawEntry>& entries,
                            const uuids::uuid& terrain = terrainUUID,
                            const net::MessageType type = net::MessageType::terrainEdit)
  {
    net::Message message(type);
    message.write(terrain);
    message.write(strokeId);
    message.write(static_cast<uint8_t>(terrain::TerrainEditKind::setChunks));
    message.write(count);

    for (const RawEntry& entry : entries)
    {
      message.write(entry.coord.x).write(entry.coord.y).write(entry.coord.z);
      message.write(entry.hash);
      message.write(entry.size);
      message.appendBytes(entry.bytes);
    }

    return message;
  }

  net::Message rawSetChunks(const std::vector<RawEntry>& entries)
  {
    return rawSetChunks(static_cast<uint32_t>(entries.size()), entries);
  }

  net::Message rawRedistance(const terrain::SampleBox& box)
  {
    net::Message message(net::MessageType::terrainEdit);
    message.write(terrainUUID);
    message.write(strokeId);
    message.write(static_cast<uint8_t>(terrain::TerrainEditKind::redistance));
    message.write(box.min.x).write(box.min.y).write(box.min.z);
    message.write(box.max.x).write(box.max.y).write(box.max.z);
    return message;
  }

  std::vector<uint8_t> bytesOf(const net::Message& message)
  {
    const auto span = message.bytes();
    return { span.begin(), span.end() };
  }

  bool accepted(const net::Message& message)
  {
    return terrain::unpackTerrainEdit(message).has_value();
  }

  RawEntry validRaw(const terrain::ChunkCoord coord = { 1, 2, 3 })
  {
    return rawFor(coord, solidChunk());
  }
}

TEST(TerrainEdit, EntryHeaderBytesMatchTheLayout)
{
  EXPECT_EQ(terrain::setChunkEntryHeaderBytes, 24u);

  const auto chunk = solidChunk();
  const std::vector<terrain::SetChunkEntry> entries{ { { 0, 0, 0 }, chunk } };
  EXPECT_EQ(terrain::packSetChunksEdit(terrainUUID, strokeId, entries).size(),
            headerBytes + sizeof(uint32_t) + terrain::setChunkEntryHeaderBytes + terrain::encodeChunk(*chunk).size());
}

TEST(TerrainEdit, BrushRoundTripsEveryField)
{
  using Kind = terrain::BrushOp::Kind;

  for (const Kind kind : { Kind::add, Kind::subtract, Kind::smooth, Kind::flatten, Kind::paint })
  {
    terrain::BrushOp op = sampleBrush();
    op.kind = kind;

    const net::Message message = terrain::packBrushEdit(terrainUUID, strokeId, op);
    EXPECT_EQ(message.getType(), net::MessageType::terrainEdit);

    const auto payload = terrain::unpackTerrainEdit(message);
    ASSERT_TRUE(payload.has_value());
    EXPECT_EQ(payload->terrain, terrainUUID);
    EXPECT_EQ(payload->strokeId, strokeId);
    EXPECT_EQ(payload->kind, terrain::TerrainEditKind::brush);
    EXPECT_EQ(payload->brush.kind, op.kind);
    EXPECT_EQ(payload->brush.center, op.center);
    EXPECT_EQ(payload->brush.radius, op.radius);
    EXPECT_EQ(payload->brush.strength, op.strength);
    EXPECT_EQ(payload->brush.falloff, op.falloff);
    EXPECT_EQ(payload->brush.material, op.material);
    EXPECT_EQ(payload->brush.planePoint, op.planePoint);
    EXPECT_EQ(payload->brush.planeNormal, op.planeNormal);
    EXPECT_TRUE(payload->chunks.empty());
  }
}

TEST(TerrainEdit, RedistanceRoundTripsTheBox)
{
  const terrain::SampleBox box = sampleBox();
  const auto payload = terrain::unpackTerrainEdit(terrain::packRedistanceEdit(terrainUUID, strokeId, box));

  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ(payload->terrain, terrainUUID);
  EXPECT_EQ(payload->strokeId, strokeId);
  EXPECT_EQ(payload->kind, terrain::TerrainEditKind::redistance);
  EXPECT_EQ(payload->box.min, box.min);
  EXPECT_EQ(payload->box.max, box.max);
  EXPECT_TRUE(payload->chunks.empty());
}

TEST(TerrainEdit, SetChunksRoundTripsDenseUniformAndNullEntries)
{
  const auto dense = denseChunk();
  const auto solid = solidChunk();
  const auto air = terrain::TerrainChunk::uniform(terrain::airDistance, 0);

  const std::vector<terrain::SetChunkEntry> entries{
    { { 0, 0, 0 }, dense },
    { { -1, 2, 3 }, solid },
    { { 4, 4, 4 }, nullptr }
  };

  const auto payload = terrain::unpackTerrainEdit(terrain::packSetChunksEdit(terrainUUID, strokeId, entries));
  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ(payload->terrain, terrainUUID);
  EXPECT_EQ(payload->strokeId, strokeId);
  EXPECT_EQ(payload->kind, terrain::TerrainEditKind::setChunks);
  ASSERT_EQ(payload->chunks.size(), 3u);

  EXPECT_EQ(payload->chunks[0].coord, (terrain::ChunkCoord{ 0, 0, 0 }));
  ASSERT_NE(payload->chunks[0].chunk, nullptr);
  EXPECT_EQ(payload->chunks[0].chunk->hash(), dense->hash());

  EXPECT_EQ(payload->chunks[1].coord, (terrain::ChunkCoord{ -1, 2, 3 }));
  ASSERT_NE(payload->chunks[1].chunk, nullptr);
  EXPECT_EQ(payload->chunks[1].chunk->hash(), solid->hash());
  EXPECT_FALSE(payload->chunks[1].chunk->isAir());

  EXPECT_EQ(payload->chunks[2].coord, (terrain::ChunkCoord{ 4, 4, 4 }));
  ASSERT_NE(payload->chunks[2].chunk, nullptr);
  EXPECT_TRUE(payload->chunks[2].chunk->isAir());
  EXPECT_EQ(payload->chunks[2].chunk->hash(), air->hash());
}

TEST(TerrainEdit, PackSetChunksThrowsOnlyAboveTheEntryLimit)
{
  const auto solid = solidChunk();
  std::vector<terrain::SetChunkEntry> entries(terrain::maxTerrainChunksEntries, { { 0, 0, 0 }, solid });

  EXPECT_NO_THROW(static_cast<void>(terrain::packSetChunksEdit(terrainUUID, strokeId, entries)));

  entries.push_back({ { 0, 0, 0 }, solid });
  EXPECT_THROW(static_cast<void>(terrain::packSetChunksEdit(terrainUUID, strokeId, entries)), std::length_error);
}

TEST(TerrainEdit, MessageTypeIsAMutationNamedTerrainEdit)
{
  EXPECT_EQ(net::messageTypeName(net::MessageType::terrainEdit), "terrainEdit");
  EXPECT_TRUE(net::isMutationMessage(net::MessageType::terrainEdit));
  EXPECT_EQ(terrain::packBrushEdit(terrainUUID, strokeId, sampleBrush()).getType(), net::MessageType::terrainEdit);
  EXPECT_EQ(terrain::packRedistanceEdit(terrainUUID, strokeId, sampleBox()).getType(),
            net::MessageType::terrainEdit);
  EXPECT_EQ(terrain::packSetChunksEdit(terrainUUID, strokeId, {}).getType(), net::MessageType::terrainEdit);
}

TEST(TerrainEdit, RefusesAnotherMessageType)
{
  EXPECT_TRUE(accepted(rawSetChunks(std::vector<RawEntry>{ validRaw() })));

  const auto bytes = bytesOf(rawSetChunks(std::vector<RawEntry>{ validRaw() }));
  EXPECT_FALSE(accepted(net::Message(net::MessageType::terrainChunks, bytes)));
  EXPECT_FALSE(accepted(net::Message(net::MessageType::undefined, bytes)));
}

TEST(TerrainEdit, RefusesANilTerrainUuid)
{
  EXPECT_TRUE(accepted(rawSetChunks(1, { validRaw() }, terrainUUID)));
  EXPECT_FALSE(accepted(rawSetChunks(1, { validRaw() }, uuids::uuid{})));
  EXPECT_FALSE(accepted(terrain::packBrushEdit(uuids::uuid{}, strokeId, sampleBrush())));
  EXPECT_FALSE(accepted(terrain::packRedistanceEdit(uuids::uuid{}, strokeId, sampleBox())));
}

TEST(TerrainEdit, RefusesAnUnknownKind)
{
  const net::Message good = terrain::packRedistanceEdit(terrainUUID, strokeId, sampleBox());
  EXPECT_TRUE(accepted(good));

  for (const uint8_t kind : { uint8_t{ 3 }, uint8_t{ 4 }, uint8_t{ 255 } })
  {
    auto bytes = bytesOf(good);
    bytes[headerBytes - 1] = kind;
    EXPECT_FALSE(accepted(net::Message(net::MessageType::terrainEdit, bytes))) << static_cast<int>(kind);
  }
}

TEST(TerrainEdit, RefusesABrushAReaderCannotTrust)
{
  EXPECT_TRUE(accepted(terrain::packBrushEdit(terrainUUID, strokeId, sampleBrush())));

  terrain::BrushOp zeroRadius = sampleBrush();
  zeroRadius.radius = 0.0f;
  EXPECT_FALSE(accepted(terrain::packBrushEdit(terrainUUID, strokeId, zeroRadius)));

  terrain::BrushOp strong = sampleBrush();
  strong.strength = 1.5f;
  EXPECT_FALSE(accepted(terrain::packBrushEdit(terrainUUID, strokeId, strong)));

  terrain::BrushOp badKind = sampleBrush();
  badKind.kind = static_cast<terrain::BrushOp::Kind>(9);
  EXPECT_FALSE(accepted(terrain::packBrushEdit(terrainUUID, strokeId, badKind)));
}

TEST(TerrainEdit, RefusesAnEmptyRedistanceBox)
{
  EXPECT_TRUE(accepted(rawRedistance({ { 0, 0, 0 }, { 1, 1, 1 } })));

  for (int axis = 0; axis < 3; ++axis)
  {
    terrain::SampleBox flat{ { 0, 0, 0 }, { 5, 5, 5 } };
    flat.max[axis] = 0;
    EXPECT_FALSE(accepted(rawRedistance(flat))) << axis;

    terrain::SampleBox inverted{ { 0, 0, 0 }, { 5, 5, 5 } };
    inverted.max[axis] = -3;
    EXPECT_FALSE(accepted(rawRedistance(inverted))) << axis;
  }
}

TEST(TerrainEdit, RefusesASetChunksCountOfZero)
{
  EXPECT_TRUE(accepted(rawSetChunks(1, { validRaw() })));
  EXPECT_FALSE(accepted(rawSetChunks(0, {})));
  EXPECT_FALSE(accepted(terrain::packSetChunksEdit(terrainUUID, strokeId, {})));
}

TEST(TerrainEdit, RefusesASetChunksCountAboveTheLimit)
{
  const RawEntry sample = validRaw();
  std::vector<RawEntry> entries;

  for (size_t i = 0; i < terrain::maxTerrainChunksEntries; ++i)
  {
    RawEntry entry = sample;
    entry.coord = { static_cast<int32_t>(i), 0, 0 };
    entries.push_back(entry);
  }

  EXPECT_TRUE(accepted(rawSetChunks(entries)));

  RawEntry extra = sample;
  extra.coord = { static_cast<int32_t>(terrain::maxTerrainChunksEntries), 0, 0 };
  entries.push_back(extra);
  EXPECT_FALSE(accepted(rawSetChunks(entries)));
}

TEST(TerrainEdit, RefusesACountTheRemainingBytesCannotHold)
{
  EXPECT_TRUE(accepted(rawSetChunks(1, { validRaw() })));
  EXPECT_FALSE(accepted(rawSetChunks(2, { validRaw() })));
  EXPECT_FALSE(accepted(rawSetChunks(0xFFFFFFFFu, { validRaw() })));
}

TEST(TerrainEdit, RefusesACoordinateBeyondTheValidRange)
{
  EXPECT_TRUE(accepted(rawSetChunks({ validRaw({ terrain::maxChunkCoord, -terrain::maxChunkCoord, 0 }) })));

  EXPECT_FALSE(accepted(rawSetChunks({ validRaw({ terrain::maxChunkCoord + 1, 0, 0 }) })));
  EXPECT_FALSE(accepted(rawSetChunks({ validRaw({ 0, -terrain::maxChunkCoord - 1, 0 }) })));
  EXPECT_FALSE(accepted(rawSetChunks({ validRaw({ 0, 0, terrain::maxChunkCoord + 1 }) })));
}

TEST(TerrainEdit, RefusesARepeatedCoordinate)
{
  EXPECT_TRUE(accepted(rawSetChunks({ validRaw({ 1, 2, 3 }), validRaw({ 1, 2, 4 }) })));
  EXPECT_FALSE(accepted(rawSetChunks({ validRaw({ 1, 2, 3 }), validRaw({ 1, 2, 3 }) })));
}

TEST(TerrainEdit, RefusesAnEntrySizeOfZeroOrAboveTheLimit)
{
  EXPECT_TRUE(accepted(rawSetChunks({ validRaw() })));

  RawEntry empty = validRaw();
  empty.size = 0;
  empty.bytes.clear();
  EXPECT_FALSE(accepted(rawSetChunks({ empty })));

  RawEntry oversized = validRaw();
  oversized.size = static_cast<uint32_t>(terrain::maxEncodedChunkBytes + 1);
  oversized.bytes.resize(oversized.size, 0);
  EXPECT_FALSE(accepted(rawSetChunks({ oversized })));

  RawEntry pastTheEnd = validRaw();
  pastTheEnd.size = static_cast<uint32_t>(pastTheEnd.bytes.size() + 1);
  EXPECT_FALSE(accepted(rawSetChunks({ pastTheEnd })));
}

TEST(TerrainEdit, RefusesBytesTheCodecRefuses)
{
  const RawEntry good = rawFor({ 1, 2, 3 }, denseChunk());
  EXPECT_TRUE(accepted(rawSetChunks({ good })));

  RawEntry truncated = good;
  truncated.bytes.pop_back();
  truncated.size = static_cast<uint32_t>(truncated.bytes.size());
  EXPECT_FALSE(accepted(rawSetChunks({ truncated })));

  RawEntry garbage = good;
  garbage.bytes.assign(garbage.bytes.size(), 0xFF);
  EXPECT_FALSE(accepted(rawSetChunks({ garbage })));
}

TEST(TerrainEdit, RefusesAHashThatDiffersFromTheDecodedChunk)
{
  const net::Message good = rawSetChunks({ validRaw() });
  EXPECT_TRUE(accepted(good));

  auto bytes = bytesOf(good);
  bytes[firstHashOffset] ^= 0x01;
  EXPECT_FALSE(accepted(net::Message(net::MessageType::terrainEdit, bytes)));
}

TEST(TerrainEdit, RefusesTrailingBytes)
{
  const std::vector<net::Message> goods{
    terrain::packBrushEdit(terrainUUID, strokeId, sampleBrush()),
    terrain::packRedistanceEdit(terrainUUID, strokeId, sampleBox()),
    rawSetChunks({ validRaw() })
  };

  for (const net::Message& good : goods)
  {
    EXPECT_TRUE(accepted(good));

    auto bytes = bytesOf(good);
    bytes.push_back(0);
    EXPECT_FALSE(accepted(net::Message(net::MessageType::terrainEdit, bytes)));
  }
}

TEST(TerrainEdit, RefusesEveryTruncation)
{
  const std::vector<net::Message> goods{
    terrain::packBrushEdit(terrainUUID, strokeId, sampleBrush()),
    terrain::packRedistanceEdit(terrainUUID, strokeId, sampleBox()),
    rawSetChunks({ validRaw(), validRaw({ 5, 6, 7 }) })
  };

  for (const net::Message& good : goods)
  {
    ASSERT_TRUE(accepted(good));

    const auto bytes = good.bytes();
    for (size_t length = 0; length < bytes.size(); ++length)
    {
      const net::Message prefix(net::MessageType::terrainEdit, bytes.first(length));
      EXPECT_FALSE(accepted(prefix)) << length;
    }
  }
}
