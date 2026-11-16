#include <gtest/gtest.h>

#include "WireTypes.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainCodec.h"
#include "terrain/TerrainReplication.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainTypes.h"

#include <Protocol.h>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
  const auto terrainUUID = uuids::uuid::from_string("11111111-1111-1111-1111-111111111111").value();

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

  struct RawEntry
  {
    terrain::ChunkCoord coord;
    uint32_t revision = 1;
    uint64_t hash = 0;
    uint32_t size = 0;
    std::vector<uint8_t> bytes;
  };

  RawEntry rawFrom(const terrain::ChunkEntry& entry)
  {
    return { entry.coord, entry.revision, entry.hash, static_cast<uint32_t>(entry.encoded.size()), entry.encoded };
  }

  net::Message rawMessage(const uuids::uuid& terrain, const uint32_t count, const std::vector<RawEntry>& entries,
                          const net::MessageType type = net::MessageType::terrainChunks)
  {
    net::Message message(type);
    message.write(terrain);
    message.write(count);

    for (const RawEntry& entry : entries)
    {
      message.write(entry.coord.x).write(entry.coord.y).write(entry.coord.z);
      message.write(entry.revision);
      message.write(entry.hash);
      message.write(entry.size);
      message.appendBytes(entry.bytes);
    }

    return message;
  }

  net::Message rawMessage(const std::vector<RawEntry>& entries)
  {
    return rawMessage(terrainUUID, static_cast<uint32_t>(entries.size()), entries);
  }

  RawEntry validRaw(const terrain::ChunkCoord coord = { 1, 2, 3 })
  {
    return rawFrom(terrain::makeChunkEntry(coord, 4, solidChunk()));
  }

  bool accepted(const net::Message& message)
  {
    return terrain::unpackTerrainChunks(message).has_value();
  }
}

TEST(TerrainReplication, EntryHeaderAndPayloadHeaderSizesMatchTheLayout)
{
  EXPECT_EQ(terrain::chunkEntryHeaderBytes, 28u);
  EXPECT_EQ(terrain::terrainChunksHeaderBytes, 20u);

  const auto entry = terrain::makeChunkEntry({ 0, 0, 0 }, 1, solidChunk());
  const std::vector<terrain::ChunkEntry> entries{ entry };
  EXPECT_EQ(terrain::packTerrainChunks(terrainUUID, entries).size(),
            terrain::terrainChunksHeaderBytes + terrain::chunkEntryHeaderBytes + entry.encoded.size());
}

TEST(TerrainReplication, DenseUniformAirAndHashOnlyEntriesRoundTrip)
{
  const auto dense = denseChunk();
  const auto solid = solidChunk();

  const std::vector<terrain::ChunkEntry> entries{
    terrain::makeChunkEntry({ 0, 0, 0 }, 5, dense),
    terrain::makeChunkEntry({ -1, 2, 3 }, 6, solid),
    terrain::makeChunkEntry({ 4, 4, 4 }, 7, nullptr),
    terrain::makeHashOnlyEntry({ 9, -9, 9 }, 8, 0x1234567890ABCDEFull)
  };

  const auto payload = terrain::unpackTerrainChunks(terrain::packTerrainChunks(terrainUUID, entries));
  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ(payload->terrain, terrainUUID);
  ASSERT_EQ(payload->entries.size(), 4u);

  ASSERT_NE(payload->entries[0].chunk, nullptr);
  EXPECT_EQ(payload->entries[0].chunk->hash(), dense->hash());
  EXPECT_EQ(payload->entries[0].revision, 5u);
  EXPECT_EQ(payload->entries[0].hash, dense->hash());

  ASSERT_NE(payload->entries[1].chunk, nullptr);
  EXPECT_EQ(payload->entries[1].chunk->hash(), solid->hash());
  EXPECT_TRUE(payload->entries[1].chunk->isUniform());
  EXPECT_EQ(payload->entries[1].coord, (terrain::ChunkCoord{ -1, 2, 3 }));

  ASSERT_NE(payload->entries[2].chunk, nullptr);
  EXPECT_TRUE(payload->entries[2].chunk->isAir());
  EXPECT_EQ(payload->entries[2].revision, 7u);

  EXPECT_EQ(payload->entries[3].chunk, nullptr);
  EXPECT_EQ(payload->entries[3].hash, 0x1234567890ABCDEFull);
  EXPECT_EQ(payload->entries[3].revision, 8u);
  EXPECT_EQ(payload->entries[3].coord, (terrain::ChunkCoord{ 9, -9, 9 }));
}

TEST(TerrainReplication, AnEmptyPayloadRoundTrips)
{
  const auto payload = terrain::unpackTerrainChunks(terrain::packTerrainChunks(terrainUUID, {}));
  ASSERT_TRUE(payload.has_value());
  EXPECT_TRUE(payload->entries.empty());
}

TEST(TerrainReplication, EveryTruncatedPrefixIsRefused)
{
  const std::vector<terrain::ChunkEntry> entries{
    terrain::makeChunkEntry({ 0, 0, 0 }, 1, solidChunk()),
    terrain::makeHashOnlyEntry({ 1, 0, 0 }, 2, 77)
  };
  const auto full = terrain::packTerrainChunks(terrainUUID, entries);
  ASSERT_TRUE(accepted(full));

  for (size_t length = 0; length < full.size(); ++length)
  {
    const net::Message prefix(net::MessageType::terrainChunks, full.bytes().first(length));
    EXPECT_FALSE(accepted(prefix)) << length;
  }
}

TEST(TerrainReplication, AForgedHugeEntryCountIsRefused)
{
  ASSERT_TRUE(accepted(rawMessage({ validRaw() })));

  EXPECT_FALSE(accepted(rawMessage(terrainUUID, 0xFFFFFFFFu, { validRaw() })));
  EXPECT_FALSE(accepted(rawMessage(terrainUUID, 2, { validRaw() })));
}

TEST(TerrainReplication, MoreThanTheEntryCapIsRefusedOnUnpackAndThrownOnPack)
{
  std::vector<terrain::ChunkEntry> entries;
  for (size_t i = 0; i < terrain::maxTerrainChunksEntries; ++i)
  {
    entries.push_back(terrain::makeChunkEntry({ static_cast<int32_t>(i), 0, 0 }, 1, solidChunk()));
  }

  const auto atCap = terrain::unpackTerrainChunks(terrain::packTerrainChunks(terrainUUID, entries));
  ASSERT_TRUE(atCap.has_value());
  EXPECT_EQ(atCap->entries.size(), terrain::maxTerrainChunksEntries);

  std::vector<RawEntry> raw;
  for (const auto& entry : entries)
  {
    raw.push_back(rawFrom(entry));
  }
  raw.push_back(rawFrom(terrain::makeChunkEntry({ -1, 0, 0 }, 1, solidChunk())));
  EXPECT_FALSE(accepted(rawMessage(raw)));

  entries.push_back(terrain::makeChunkEntry({ -1, 0, 0 }, 1, solidChunk()));
  EXPECT_THROW(static_cast<void>(terrain::packTerrainChunks(terrainUUID, entries)), std::length_error);
}

TEST(TerrainReplication, AnEncodedSizeOverTheMaximumIsRefused)
{
  ASSERT_TRUE(accepted(rawMessage({ validRaw() })));

  RawEntry entry = validRaw();
  entry.size = static_cast<uint32_t>(terrain::maxEncodedChunkBytes + 1);
  entry.bytes.assign(terrain::maxEncodedChunkBytes + 1, 0);
  EXPECT_FALSE(accepted(rawMessage({ entry })));
}

TEST(TerrainReplication, AnEncodedSizePastTheEndIsRefused)
{
  ASSERT_TRUE(accepted(rawMessage({ validRaw() })));

  RawEntry entry = validRaw();
  entry.size += 1;
  EXPECT_FALSE(accepted(rawMessage({ entry })));
}

TEST(TerrainReplication, CorruptEncodedBytesAreRefused)
{
  const auto encoded = terrain::makeChunkEntry({ 0, 0, 0 }, 1, denseChunk());
  ASSERT_TRUE(accepted(rawMessage({ rawFrom(encoded) })));

  RawEntry entry = rawFrom(encoded);
  entry.bytes.resize(entry.bytes.size() / 2);
  entry.size = static_cast<uint32_t>(entry.bytes.size());
  EXPECT_FALSE(accepted(rawMessage({ entry })));
}

TEST(TerrainReplication, AHashThatDoesNotMatchTheDecodedChunkIsRefused)
{
  ASSERT_TRUE(accepted(rawMessage({ validRaw() })));

  RawEntry entry = validRaw();
  entry.hash += 1;
  EXPECT_FALSE(accepted(rawMessage({ entry })));
}

TEST(TerrainReplication, ARepeatedCoordinateIsRefused)
{
  ASSERT_TRUE(accepted(rawMessage({ validRaw({ 1, 1, 1 }), validRaw({ 2, 1, 1 }) })));

  EXPECT_FALSE(accepted(rawMessage({ validRaw({ 1, 1, 1 }), validRaw({ 1, 1, 1 }) })));
}

TEST(TerrainReplication, ACoordinateBeyondTheMaximumIsRefused)
{
  ASSERT_TRUE(accepted(rawMessage({ validRaw({ terrain::maxChunkCoord, 0, 0 }) })));

  EXPECT_FALSE(accepted(rawMessage({ validRaw({ terrain::maxChunkCoord + 1, 0, 0 }) })));
  EXPECT_FALSE(accepted(rawMessage({ validRaw({ 0, -terrain::maxChunkCoord - 1, 0 }) })));
}

TEST(TerrainReplication, RevisionZeroIsRefused)
{
  ASSERT_TRUE(accepted(rawMessage({ validRaw() })));

  RawEntry entry = validRaw();
  entry.revision = 0;
  EXPECT_FALSE(accepted(rawMessage({ entry })));
}

TEST(TerrainReplication, ATrailingByteIsRefused)
{
  const auto good = rawMessage({ validRaw() });
  ASSERT_TRUE(accepted(good));

  net::Message padded(net::MessageType::terrainChunks);
  padded.appendBytes(good.bytes());
  padded.write<uint8_t>(0);
  EXPECT_FALSE(accepted(padded));
}

TEST(TerrainReplication, AWrongMessageTypeIsRefused)
{
  ASSERT_TRUE(accepted(rawMessage({ validRaw() })));

  EXPECT_FALSE(accepted(rawMessage(terrainUUID, 1, { validRaw() }, net::MessageType::snapshot)));
}

TEST(TerrainReplication, ANilTerrainUuidIsRefused)
{
  ASSERT_TRUE(accepted(rawMessage({ validRaw() })));

  EXPECT_FALSE(accepted(rawMessage(uuids::uuid{}, 1, { validRaw() })));
}

namespace {
  terrain::TerrainStore storeWithTerrain()
  {
    terrain::TerrainStore store;
    EXPECT_NE(store.create({ .uuid = terrainUUID }), nullptr);
    return store;
  }

  terrain::TerrainChunksPayload payloadOf(const std::vector<terrain::ChunkEntry>& entries)
  {
    const auto payload = terrain::unpackTerrainChunks(terrain::packTerrainChunks(terrainUUID, entries));
    EXPECT_TRUE(payload.has_value());
    return payload.value_or(terrain::TerrainChunksPayload{});
  }

  constexpr terrain::ChunkCoord origin{ 0, 0, 0 };
}

TEST(TerrainApply, AHigherRevisionAppliesAndRecordsIt)
{
  auto store = storeWithTerrain();
  const auto solid = solidChunk();

  const auto result = terrain::applyTerrainChunks(store, payloadOf({ terrain::makeChunkEntry(origin, 3, solid) }));

  EXPECT_EQ(result.applied, 1u);
  EXPECT_EQ(result.stale, 0u);
  ASSERT_NE(store.find(terrainUUID)->chunk(origin), nullptr);
  EXPECT_EQ(store.find(terrainUUID)->chunk(origin)->hash(), solid->hash());
  EXPECT_EQ(store.revision(terrainUUID, origin), 3u);

  const auto newer = terrain::applyTerrainChunks(store, payloadOf({ terrain::makeChunkEntry(origin, 4, denseChunk()) }));
  EXPECT_EQ(newer.applied, 1u);
  EXPECT_EQ(store.find(terrainUUID)->chunk(origin)->hash(), denseChunk()->hash());
}

TEST(TerrainApply, LowerAndEqualRevisionsAreStaleAndChangeNothing)
{
  auto store = storeWithTerrain();
  const auto solid = solidChunk();
  ASSERT_EQ(terrain::applyTerrainChunks(store, payloadOf({ terrain::makeChunkEntry(origin, 5, solid) })).applied, 1u);

  for (const uint32_t revision : { 5u, 2u })
  {
    const auto result =
      terrain::applyTerrainChunks(store, payloadOf({ terrain::makeChunkEntry(origin, revision, denseChunk()) }));
    EXPECT_EQ(result.stale, 1u) << revision;
    EXPECT_EQ(result.applied, 0u) << revision;
    EXPECT_EQ(store.find(terrainUUID)->chunk(origin)->hash(), solid->hash()) << revision;
    EXPECT_EQ(store.revision(terrainUUID, origin), 5u) << revision;
  }
}

TEST(TerrainApply, AHashOnlyEntryMatchingTheHeldChunkRecordsTheRevision)
{
  auto store = storeWithTerrain();
  const auto solid = solidChunk();
  ASSERT_EQ(terrain::applyTerrainChunks(store, payloadOf({ terrain::makeChunkEntry(origin, 1, solid) })).applied, 1u);

  const auto result =
    terrain::applyTerrainChunks(store, payloadOf({ terrain::makeHashOnlyEntry(origin, 6, solid->hash()) }));

  EXPECT_EQ(result.applied, 1u);
  EXPECT_TRUE(result.missing.empty());
  EXPECT_EQ(store.revision(terrainUUID, origin), 6u);
  EXPECT_EQ(store.find(terrainUUID)->chunk(origin)->hash(), solid->hash());
}

TEST(TerrainApply, AHashOnlyEntryForAMissingOrDifferentChunkIsReportedAndNotRecorded)
{
  auto store = storeWithTerrain();
  const auto solid = solidChunk();
  const terrain::ChunkCoord held{ 1, 0, 0 };
  const terrain::ChunkCoord lacking{ 2, 0, 0 };
  ASSERT_EQ(terrain::applyTerrainChunks(store, payloadOf({ terrain::makeChunkEntry(held, 1, solid) })).applied, 1u);

  const auto result = terrain::applyTerrainChunks(
    store, payloadOf({ terrain::makeHashOnlyEntry(held, 9, denseChunk()->hash()),
                       terrain::makeHashOnlyEntry(lacking, 9, solid->hash()) }));

  EXPECT_EQ(result.applied, 0u);
  ASSERT_EQ(result.missing.size(), 2u);
  EXPECT_EQ(result.missing[0], held);
  EXPECT_EQ(result.missing[1], lacking);
  EXPECT_EQ(store.revision(terrainUUID, held), 1u);
  EXPECT_EQ(store.revision(terrainUUID, lacking), 0u);
  EXPECT_EQ(store.find(terrainUUID)->chunk(lacking), nullptr);
}

TEST(TerrainApply, AHashOnlyAirEntryForAnAbsentCoordinateIsAccepted)
{
  auto store = storeWithTerrain();
  const auto airHash = terrain::makeChunkEntry(origin, 1, nullptr).hash;

  const auto result = terrain::applyTerrainChunks(store, payloadOf({ terrain::makeHashOnlyEntry(origin, 2, airHash) }));

  EXPECT_EQ(result.applied, 1u);
  EXPECT_TRUE(result.missing.empty());
  EXPECT_EQ(store.revision(terrainUUID, origin), 2u);
  EXPECT_EQ(store.find(terrainUUID)->chunk(origin), nullptr);
}

TEST(TerrainApply, AnAirFullEntryErasesTheChunk)
{
  auto store = storeWithTerrain();
  ASSERT_EQ(terrain::applyTerrainChunks(store, payloadOf({ terrain::makeChunkEntry(origin, 1, solidChunk()) })).applied,
            1u);
  ASSERT_NE(store.find(terrainUUID)->chunk(origin), nullptr);

  const auto result = terrain::applyTerrainChunks(store, payloadOf({ terrain::makeChunkEntry(origin, 2, nullptr) }));

  EXPECT_EQ(result.applied, 1u);
  EXPECT_EQ(store.find(terrainUUID)->chunk(origin), nullptr);
  EXPECT_EQ(store.revision(terrainUUID, origin), 2u);

  const auto older = terrain::applyTerrainChunks(store, payloadOf({ terrain::makeChunkEntry(origin, 1, solidChunk()) }));
  EXPECT_EQ(older.stale, 1u);
  EXPECT_EQ(store.find(terrainUUID)->chunk(origin), nullptr);
}

TEST(TerrainApply, UnknownTerrainIsFlaggedAndChangesNothing)
{
  auto store = storeWithTerrain();
  const auto payload = payloadOf({ terrain::makeChunkEntry(origin, 1, solidChunk()) });

  terrain::TerrainChunksPayload other = payload;
  other.terrain = uuids::uuid::from_string("22222222-2222-2222-2222-222222222222").value();

  const auto unknown = terrain::applyTerrainChunks(store, other);
  EXPECT_TRUE(unknown.unknownTerrain);
  EXPECT_EQ(unknown.applied, 0u);
  EXPECT_EQ(store.find(terrainUUID)->chunk(origin), nullptr);

  const auto known = terrain::applyTerrainChunks(store, payload);
  EXPECT_FALSE(known.unknownTerrain);
  EXPECT_EQ(known.applied, 1u);
}
