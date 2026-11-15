#include <gtest/gtest.h>

#include "terrain/TerrainChunk.h"
#include "terrain/TerrainFormat.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace {
  const uuids::uuid terrainUuid = uuids::uuid::from_string("0A1B2C3D-0000-4000-8000-00000000ABCD").value();

  std::shared_ptr<const terrain::TerrainChunk> denseChunk(const uint32_t seed)
  {
    std::mt19937 rng(seed);
    std::vector<int16_t> distances(terrain::samplesPerChunk);
    std::vector<uint8_t> materials(terrain::samplesPerChunk);

    for (size_t i = 0; i < distances.size(); ++i)
    {
      distances[i] = static_cast<int16_t>(static_cast<int>(rng() % (2 * terrain::airDistance + 1)) -
                                          terrain::airDistance);
      materials[i] = static_cast<uint8_t>(rng() % 4);
    }

    return terrain::TerrainChunk::dense(std::move(distances), std::move(materials));
  }

  std::shared_ptr<const terrain::TerrainChunk> solidChunk()
  {
    return terrain::TerrainChunk::uniform(-terrain::airDistance, 1);
  }

  terrain::TerrainDescription makeDescription(const float voxelSize = 2.5f)
  {
    terrain::TerrainDescription description;
    description.uuid = terrainUuid;
    description.voxelSize = voxelSize;
    description.palette.push_back({ "grass", terrainUuid, uuids::uuid() });
    description.palette.push_back({ "rock", uuids::uuid(), terrainUuid });
    return description;
  }

  // Region (0,0,0) holds two chunks, so a region with several entries is covered.
  terrain::ChunksByKey twoChunks()
  {
    return { { { 0, 0, 0 }, denseChunk(1) }, { { 1, 0, 0 }, solidChunk() } };
  }

  terrain::RegionRecord recordFor(const terrain::ChunksByKey& chunks)
  {
    terrain::RegionRecord record;

    for (const auto& [coord, chunk] : chunks)
    {
      record.hashes.emplace(coord, chunk->hash());
    }

    record.tag = terrain::regionTag(record.hashes);
    record.bytes = terrain::encodeRegion(chunks).value().size();
    return record;
  }

  // Four chunks in three regions, with the region of every chunk and its real tag and size.
  terrain::TerrainIndex makeIndex(const float voxelSize = 2.5f)
  {
    terrain::TerrainIndex index;
    index.description = makeDescription(voxelSize);
    index.regions[{ 0, 0, 0 }] = recordFor(twoChunks());
    index.regions[{ -1, -1, -1 }] = recordFor({ { { -1, -1, -1 }, denseChunk(2) } });
    index.regions[{ 1, 0, 0 }] = recordFor({ { { 9, 0, 0 }, denseChunk(3) } });
    return index;
  }

  std::vector<uint8_t> bytesOf(const std::string& text)
  {
    return { text.begin(), text.end() };
  }

  std::vector<uint8_t> indexBytes(const terrain::TerrainIndex& index = makeIndex())
  {
    return bytesOf(terrain::writeIndex(index).value());
  }

  std::vector<uint8_t> editedIndexBytes(const std::function<void(nlohmann::json&)>& edit)
  {
    auto json = nlohmann::json::parse(terrain::writeIndex(makeIndex()).value());
    edit(json);
    return bytesOf(json.dump());
  }

  nlohmann::json& regionEntry(nlohmann::json& index, const nlohmann::json& key)
  {
    for (auto& entry : index["regions"])
    {
      if (entry["region"] == key)
      {
        return entry;
      }
    }

    ADD_FAILURE() << "no such region";
    return index["regions"][0];
  }

  // Parses the unedited index first, so a rejection below can only come from the edit.
  void expectIndexRejected(const std::vector<uint8_t>& text, const char* mention)
  {
    terrain::TerrainIndex parsed;
    std::string error;
    ASSERT_TRUE(terrain::parseIndex(indexBytes(), terrainUuid, parsed, error)) << error;

    terrain::TerrainIndex rejected;
    error.clear();
    EXPECT_FALSE(terrain::parseIndex(text, terrainUuid, rejected, error));
    EXPECT_NE(error.find(mention), std::string::npos) << error;
  }

  void expectRegionRejected(const std::vector<uint8_t>& bytes, const terrain::RegionRecord& record, const char* mention)
  {
    const auto chunks = twoChunks();
    const auto goodBytes = terrain::encodeRegion(chunks).value();
    terrain::TerrainVolume::ChunkMap good;
    std::string error;
    ASSERT_TRUE(terrain::decodeRegion(goodBytes, { 0, 0, 0 }, recordFor(chunks), good, error)) << error;

    terrain::TerrainVolume::ChunkMap decoded;
    error.clear();
    EXPECT_FALSE(terrain::decodeRegion(bytes, { 0, 0, 0 }, record, decoded, error));
    EXPECT_NE(error.find(mention), std::string::npos) << error;
  }
}

TEST(TerrainFormatTest, RegionOfChunkUsesFloorDivision)
{
  EXPECT_EQ(terrain::regionOfChunk({ 0, 0, 0 }), (terrain::ChunkKey{ 0, 0, 0 }));
  EXPECT_EQ(terrain::regionOfChunk({ 7, 8, -8 }), (terrain::ChunkKey{ 0, 1, -1 }));
  EXPECT_EQ(terrain::regionOfChunk({ -1, -1, -1 }), (terrain::ChunkKey{ -1, -1, -1 }));
  EXPECT_EQ(terrain::regionOfChunk({ -9, 2, 3 }), (terrain::ChunkKey{ -2, 0, 0 }));
}

TEST(TerrainFormatTest, FileNameCarriesCoordinatesAndSixteenHexTag)
{
  const std::string name = terrain::regionFileName({ -1, 2, 30 }, 0xABCDEF);
  EXPECT_EQ(name, "r.-1.2.30.0000000000abcdef.bin");
  EXPECT_TRUE(terrain::isRegionFileName(name));
}

TEST(TerrainFormatTest, NameMatcherAcceptsOwnNamesAndRejectsForeignOnes)
{
  EXPECT_TRUE(terrain::isRegionFileName("r.0.0.0.0123456789abcdef.bin"));
  EXPECT_TRUE(terrain::isRegionFileName("r.-1.-1.-1.0123456789abcdef.bin"));

  const char* foreign[] = { "notes.bin", "r.notes.bin", "r.1.2.3.xyz.bin", "r.1.2.3.0123456789ABCDEF.bin",
                            "r.1.2.0123456789abcdef.bin", "keep.tmp", "r.1.2.3.0123456789abcdef.bin.bak",
                            "r.1.2.3.0123456789abcdef.bin.tmp", "r.1.2.3.0123456789abcdef", "terrain.json", "",
                            "r.1..3.0123456789abcdef.bin", "r.1.2.99999999999.0123456789abcdef.bin" };

  for (const char* name : foreign)
  {
    EXPECT_FALSE(terrain::isRegionFileName(name)) << name;
  }
}

TEST(TerrainFormatTest, TagIsStableAndTracksTheChunkSet)
{
  const terrain::HashByChunk base = { { { 0, 0, 0 }, 11 }, { { 1, 0, 0 }, 22 } };
  const terrain::HashByChunk same = { { { 1, 0, 0 }, 22 }, { { 0, 0, 0 }, 11 } };
  EXPECT_EQ(terrain::regionTag(base), terrain::regionTag(same));

  terrain::HashByChunk otherHash = base;
  otherHash[{ 1, 0, 0 }] = 23;
  EXPECT_NE(terrain::regionTag(base), terrain::regionTag(otherHash));

  terrain::HashByChunk otherCoord = base;
  otherCoord.erase({ 1, 0, 0 });
  otherCoord[{ 2, 0, 0 }] = 22;
  EXPECT_NE(terrain::regionTag(base), terrain::regionTag(otherCoord));

  terrain::HashByChunk fewer = base;
  fewer.erase({ 1, 0, 0 });
  EXPECT_NE(terrain::regionTag(base), terrain::regionTag(fewer));
}

TEST(TerrainFormatTest, RegionBytesRoundTrip)
{
  const auto chunks = twoChunks();
  const auto bytes = terrain::encodeRegion(chunks).value();
  const auto record = recordFor(chunks);
  terrain::TerrainVolume::ChunkMap decoded;
  std::string error;

  ASSERT_TRUE(terrain::decodeRegion(bytes, { 0, 0, 0 }, record, decoded, error)) << error;
  ASSERT_EQ(decoded.size(), chunks.size());

  for (const auto& [coord, chunk] : chunks)
  {
    const auto found = decoded.find({ coord[0], coord[1], coord[2] });
    ASSERT_NE(found, decoded.end());
    EXPECT_EQ(found->second->hash(), chunk->hash());
    EXPECT_EQ(found->second->isUniform(), chunk->isUniform());
  }
}

TEST(TerrainFormatTest, TruncatedRegionBytesFail)
{
  const auto chunks = twoChunks();
  auto bytes = terrain::encodeRegion(chunks).value();
  auto record = recordFor(chunks);
  ASSERT_GT(bytes.size(), 10u);
  bytes.resize(bytes.size() - 5);

  expectRegionRejected(bytes, record, "disagrees with the index");

  record.bytes = bytes.size();
  expectRegionRejected(bytes, record, "truncated");
}

TEST(TerrainFormatTest, GarbledMagicFails)
{
  const auto chunks = twoChunks();
  auto bytes = terrain::encodeRegion(chunks).value();
  bytes[0] = 'X';

  expectRegionRejected(bytes, recordFor(chunks), "magic");
}

TEST(TerrainFormatTest, UnsupportedRegionVersionFails)
{
  const auto chunks = twoChunks();
  auto bytes = terrain::encodeRegion(chunks).value();
  bytes[4] = static_cast<uint8_t>(terrain::terrainFormatVersion + 1);

  expectRegionRejected(bytes, recordFor(chunks), "version");
}

TEST(TerrainFormatTest, HashDisagreeingWithIndexFails)
{
  const auto chunks = twoChunks();
  auto record = recordFor(chunks);
  record.hashes[{ 1, 0, 0 }] = 0x0123456789abcdefull;

  expectRegionRejected(terrain::encodeRegion(chunks).value(), record, "hash disagrees");
}

TEST(TerrainFormatTest, IndexListingAChunkTheBytesLackFails)
{
  const auto chunks = twoChunks();
  auto record = recordFor(chunks);
  record.hashes[{ 2, 0, 0 }] = 0x0123456789abcdefull;

  expectRegionRejected(terrain::encodeRegion(chunks).value(), record, "does not hold");
}

TEST(TerrainFormatTest, TrailingBytesFail)
{
  const auto chunks = twoChunks();
  auto bytes = terrain::encodeRegion(chunks).value();
  auto record = recordFor(chunks);
  bytes.push_back(0);

  expectRegionRejected(bytes, record, "disagrees with the index");

  record.bytes = bytes.size();
  expectRegionRejected(bytes, record, "trailing");
}

TEST(TerrainFormatTest, EntryCountOverTheMaximumFails)
{
  const auto chunks = twoChunks();
  auto bytes = terrain::encodeRegion(chunks).value();
  const uint32_t tooMany = static_cast<uint32_t>(terrain::maxRegionEntries) + 1;
  bytes[8] = static_cast<uint8_t>(tooMany & 0xFF);
  bytes[9] = static_cast<uint8_t>(tooMany >> 8);

  expectRegionRejected(bytes, recordFor(chunks), "too many entries");
}

TEST(TerrainFormatTest, EntryCoordOutsideItsRegionFails)
{
  const auto chunks = twoChunks();
  auto bytes = terrain::encodeRegion(chunks).value();
  bytes[terrain::regionHeaderBytes] = static_cast<uint8_t>(terrain::regionSize);

  expectRegionRejected(bytes, recordFor(chunks), "outside this region");
}

TEST(TerrainFormatTest, IndexRoundTrips)
{
  const auto source = makeIndex();
  terrain::TerrainIndex parsed;
  std::string error;

  ASSERT_TRUE(terrain::parseIndex(indexBytes(source), terrainUuid, parsed, error)) << error;

  EXPECT_EQ(parsed.description.uuid, terrainUuid);
  EXPECT_EQ(parsed.description.voxelSize, 2.5f);
  ASSERT_EQ(parsed.description.palette.size(), 2u);
  EXPECT_EQ(parsed.description.palette[0].name, "grass");
  EXPECT_EQ(parsed.description.palette[0].albedoTexture, terrainUuid);
  EXPECT_TRUE(parsed.description.palette[0].specularTexture.is_nil());
  EXPECT_EQ(parsed.description.palette[1].specularTexture, terrainUuid);

  ASSERT_EQ(parsed.regions.size(), source.regions.size());

  for (const auto& [region, record] : source.regions)
  {
    ASSERT_TRUE(parsed.regions.contains(region));
    EXPECT_EQ(parsed.regions.at(region).tag, record.tag);
    EXPECT_EQ(parsed.regions.at(region).bytes, record.bytes);
    EXPECT_EQ(parsed.regions.at(region).hashes, record.hashes);
  }
}

TEST(TerrainFormatTest, EmptyIndexRoundTrips)
{
  terrain::TerrainIndex empty;
  empty.description = makeDescription();
  terrain::TerrainIndex parsed;
  std::string error;

  ASSERT_TRUE(terrain::parseIndex(indexBytes(empty), terrainUuid, parsed, error)) << error;
  EXPECT_TRUE(parsed.regions.empty());
  EXPECT_EQ(parsed.description.palette.size(), 2u);
}

TEST(TerrainFormatTest, IndexWithWrongUuidFails)
{
  expectIndexRejected(editedIndexBytes([](nlohmann::json& json) { json["uuid"] = "11111111-1111-4111-8111-111111111111"; }),
                      "uuid");
}

TEST(TerrainFormatTest, IndexWithBadVersionFails)
{
  expectIndexRejected(editedIndexBytes([](nlohmann::json& json) { json["formatVersion"] = terrain::terrainFormatVersion + 1; }),
                      "formatVersion");
}

TEST(TerrainFormatTest, IndexThatIsNotAJsonObjectFails)
{
  expectIndexRejected(bytesOf("not json"), "not a json object");
}

TEST(TerrainFormatTest, DuplicateRegionInIndexFails)
{
  expectIndexRejected(editedIndexBytes([](nlohmann::json& json) { json["regions"].push_back(json["regions"][0]); }),
                      "repeated");
}

TEST(TerrainFormatTest, CoordOutsideItsRegionInIndexFails)
{
  expectIndexRejected(editedIndexBytes([](nlohmann::json& json) {
    regionEntry(json, nlohmann::json::array({ 0, 0, 0 }))["chunks"][0]["coord"] = nlohmann::json::array({ 8, 0, 0 });
  }), "outside its region");
}

TEST(TerrainFormatTest, IndexEntryCountOverTheMaximumFails)
{
  expectIndexRejected(editedIndexBytes([](nlohmann::json& json) {
    auto& chunks = regionEntry(json, nlohmann::json::array({ 0, 0, 0 }))["chunks"];
    const auto first = chunks[0];

    while (chunks.size() <= terrain::maxRegionEntries)
    {
      chunks.push_back(first);
    }
  }), "oversized");
}

TEST(TerrainFormatTest, IndexByteCountOutsideTheFormatFails)
{
  expectIndexRejected(editedIndexBytes([](nlohmann::json& json) {
    regionEntry(json, nlohmann::json::array({ 0, 0, 0 }))["bytes"] = terrain::maxRegionFileBytes + 1;
  }), "impossible file size");
}

TEST(TerrainFormatTest, VoxelSizeAsStringFails)
{
  expectIndexRejected(editedIndexBytes([](nlohmann::json& json) { json["voxelSize"] = "2.5"; }), "voxelSize");
}

TEST(TerrainFormatTest, VoxelSizeOutsideFloatRangeFails)
{
  expectIndexRejected(editedIndexBytes([](nlohmann::json& json) { json["voxelSize"] = 1e300; }), "range");
}

TEST(TerrainFormatTest, PaletteEntryMissingAFieldFails)
{
  expectIndexRejected(editedIndexBytes([](nlohmann::json& json) { json["palette"][0].erase("specularTexture"); }),
                      "palette");
}

TEST(TerrainFormatTest, RegionCoordAboveInt32MaxFails)
{
  expectIndexRejected(editedIndexBytes([](nlohmann::json& json) {
    regionEntry(json, nlohmann::json::array({ 0, 0, 0 }))["region"] =
      nlohmann::json::array({ static_cast<uint64_t>(4294967295u), 0, 0 });
  }), "malformed");
}

TEST(TerrainFormatTest, DeeplyNestedIndexIsRefusedButBracketsInStringsAreNot)
{
  auto index = makeIndex();
  index.description.palette[0].name = "[[[[[[[[[[[[[[[[{{{{{{{{{{\\\"[[[[";
  terrain::TerrainIndex parsed;
  std::string error;

  ASSERT_TRUE(terrain::parseIndex(indexBytes(index), terrainUuid, parsed, error)) << error;
  EXPECT_EQ(parsed.description.palette[0].name, index.description.palette[0].name);

  expectIndexRejected(bytesOf("[[[[[[[[[[]]]]]]]]]]"), "nests deeper");
}

TEST(TerrainFormatTest, OversizedIndexIsRefused)
{
  expectIndexRejected(std::vector<uint8_t>(terrain::maxIndexBytes + 1, ' '), "larger than the format allows");
}

TEST(TerrainFormatTest, VoxelSizeRoundTripsAndReadsCleanlyInTheIndex)
{
  const float sizes[] = { 0.1f, 2.5f, 0.3f, 1.0f / 3.0f };

  for (const float size : sizes)
  {
    terrain::TerrainIndex parsed;
    std::string error;
    const std::string text = terrain::writeIndex(makeIndex(size)).value();

    ASSERT_TRUE(terrain::parseIndex(bytesOf(text), terrainUuid, parsed, error)) << size << ": " << error;
    EXPECT_EQ(parsed.description.voxelSize, size);
    EXPECT_EQ(static_cast<float>(nlohmann::json::parse(text)["voxelSize"].get<double>()), size);
  }

  EXPECT_EQ(nlohmann::json::parse(terrain::writeIndex(makeIndex(0.1f)).value())["voxelSize"].get<double>(), 0.1);
}

namespace {
  using terrain::ChunkKey;

  nlohmann::json nestedArray(const int depth)
  {
    nlohmann::json value = nlohmann::json::array();

    for (int i = 1; i < depth; ++i)
    {
      value = nlohmann::json::array({ value });
    }

    return value;
  }

  void putU32(std::vector<uint8_t>& out, const uint32_t value)
  {
    for (int shift = 0; shift < 32; shift += 8)
    {
      out.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
    }
  }

  struct RawEntry
  {
    ChunkKey coord;
    std::vector<uint8_t> encoded;
    std::optional<uint32_t> claimedLength;
  };

  // Hand-built region bytes, so an entry can be malformed in ways encodeRegion never writes.
  std::vector<uint8_t> rawRegion(const std::vector<RawEntry>& entries)
  {
    std::vector<uint8_t> out = { 'E', '3', 'T', 'R' };
    putU32(out, static_cast<uint32_t>(terrain::terrainFormatVersion));
    putU32(out, static_cast<uint32_t>(entries.size()));

    for (const RawEntry& entry : entries)
    {
      for (const int32_t component : entry.coord)
      {
        putU32(out, static_cast<uint32_t>(component));
      }

      putU32(out, entry.claimedLength.value_or(static_cast<uint32_t>(entry.encoded.size())));
      out.insert(out.end(), entry.encoded.begin(), entry.encoded.end());
    }

    return out;
  }

  void expectRawRegionRejected(const std::vector<RawEntry>& entries, const terrain::HashByChunk& hashes,
                               const char* mention)
  {
    const auto bytes = rawRegion(entries);
    terrain::RegionRecord record;
    record.hashes = hashes;
    record.tag = terrain::regionTag(hashes);
    record.bytes = bytes.size();

    terrain::TerrainVolume::ChunkMap decoded;
    std::string error;
    EXPECT_FALSE(terrain::decodeRegion(bytes, { 0, 0, 0 }, record, decoded, error));
    EXPECT_NE(error.find(mention), std::string::npos) << error;
    EXPECT_TRUE(decoded.empty());
  }

  RawEntry rawEntry(const ChunkKey& coord, const std::shared_ptr<const terrain::TerrainChunk>& chunk)
  {
    return { coord, terrain::encodeChunk(*chunk), std::nullopt };
  }
}

TEST(TerrainFormatTest, EncodeRegionRefusesWhatDecodeRegionCouldNotRead)
{
  EXPECT_TRUE(terrain::encodeRegion(twoChunks()).has_value());

  EXPECT_FALSE(terrain::encodeRegion({}).has_value());
  EXPECT_FALSE(terrain::encodeRegion({ { { 0, 0, 0 }, nullptr } }).has_value());
  EXPECT_FALSE(terrain::encodeRegion({ { { 0, 0, 0 }, terrain::TerrainChunk::uniform(terrain::airDistance, 0) } })
                 .has_value());
  EXPECT_FALSE(terrain::encodeRegion({ { { 0, 0, 0 }, solidChunk() }, { { 8, 0, 0 }, solidChunk() } }).has_value());
  EXPECT_FALSE(terrain::encodeRegion({ { { terrain::maxChunkCoord + 1, 0, 0 }, solidChunk() } }).has_value());

  terrain::ChunksByKey tooMany;

  for (int i = 0; i <= static_cast<int>(terrain::maxRegionEntries); ++i)
  {
    tooMany.emplace(ChunkKey{ i, 0, 0 }, solidChunk());
  }

  EXPECT_FALSE(terrain::encodeRegion(tooMany).has_value());
}

TEST(TerrainFormatTest, RegionBytesRoundTripWithNegativeCoordinates)
{
  const terrain::ChunksByKey chunks = { { { -8, -8, -8 }, denseChunk(4) }, { { -1, -1, -1 }, solidChunk() } };
  const auto record = recordFor(chunks);
  const auto bytes = terrain::encodeRegion(chunks).value();
  terrain::TerrainVolume::ChunkMap decoded;
  std::string error;

  ASSERT_TRUE(terrain::decodeRegion(bytes, { -1, -1, -1 }, record, decoded, error)) << error;
  ASSERT_EQ(decoded.size(), 2u);
  EXPECT_EQ(decoded.at({ -8, -8, -8 })->hash(), chunks.at({ -8, -8, -8 })->hash());
  EXPECT_TRUE(decoded.contains({ -1, -1, -1 }));

  terrain::TerrainVolume::ChunkMap other;
  EXPECT_FALSE(terrain::decodeRegion(bytes, { 0, 0, 0 }, record, other, error));
}

TEST(TerrainFormatTest, WriteIndexRefusesWhatParseIndexCouldNotRead)
{
  EXPECT_TRUE(terrain::writeIndex(makeIndex()).has_value());

  EXPECT_FALSE(terrain::writeIndex(makeIndex(0.0f)).has_value());
  EXPECT_FALSE(terrain::writeIndex(makeIndex(1e-5f)).has_value());
  EXPECT_FALSE(terrain::writeIndex(makeIndex(std::numeric_limits<float>::quiet_NaN())).has_value());
  EXPECT_FALSE(terrain::writeIndex(makeIndex(std::numeric_limits<float>::infinity())).has_value());

  auto emptyRegion = makeIndex();
  emptyRegion.regions[{ 0, 0, 0 }].hashes.clear();
  EXPECT_FALSE(terrain::writeIndex(emptyRegion).has_value());

  auto staleTag = makeIndex();
  staleTag.regions[{ 0, 0, 0 }].tag ^= 1;
  EXPECT_FALSE(terrain::writeIndex(staleTag).has_value());

  auto nilUuid = makeIndex();
  nilUuid.description.uuid = uuids::uuid();
  EXPECT_FALSE(terrain::writeIndex(nilUuid).has_value());
}

TEST(TerrainFormatTest, CodecVersionAndByteOrderArePinned)
{
  const terrain::HashByChunk entries = { { { 0, 0, 0 }, 0x0123456789abcdefull },
                                         { { -1, 2, 3 }, 0xfedcba9876543210ull } };
  // Guards the byte order and the codec version folded into the hash; changing either renames every region file.
  EXPECT_EQ(terrain::regionFileName({ 0, 0, 0 }, terrain::regionTag(entries)), "r.0.0.0.ba4556b107ae6041.bin");
  EXPECT_EQ(terrain::codecVersion, 1u);

  const auto bytes = terrain::encodeRegion(twoChunks()).value();
  const std::vector<uint8_t> prefix = { 'E', '3', 'T', 'R', 1, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  ASSERT_GE(bytes.size(), prefix.size());
  EXPECT_TRUE(std::equal(prefix.begin(), prefix.end(), bytes.begin()));
}

TEST(TerrainFormatTest, IndexWithAStaleTagFails)
{
  expectIndexRejected(editedIndexBytes([](nlohmann::json& json) {
    regionEntry(json, nlohmann::json::array({ 0, 0, 0 }))["tag"] = "0123456789abcdef";
  }), "tag does not match");
}

TEST(TerrainFormatTest, IndexWithABadTagOrHashStringFails)
{
  const char* bad[] = { "xyz", "0123456789ABCDEF", "0123456789abcde", "+123456789abcdef", "0x23456789abcdef0", "" };

  for (const char* text : bad)
  {
    expectIndexRejected(editedIndexBytes([text](nlohmann::json& json) {
      regionEntry(json, nlohmann::json::array({ 0, 0, 0 }))["tag"] = text;
    }), "malformed");
    expectIndexRejected(editedIndexBytes([text](nlohmann::json& json) {
      regionEntry(json, nlohmann::json::array({ 0, 0, 0 }))["chunks"][0]["hash"] = text;
    }), "malformed");
  }

  expectIndexRejected(editedIndexBytes([](nlohmann::json& json) {
    regionEntry(json, nlohmann::json::array({ 0, 0, 0 })).erase("tag");
  }), "malformed");
}

TEST(TerrainFormatTest, IndexWithAnInvalidDescriptionFails)
{
  expectIndexRejected(editedIndexBytes([](nlohmann::json& json) { json["voxelSize"] = 0; }), "description is invalid");
  expectIndexRejected(editedIndexBytes([](nlohmann::json& json) { json["voxelSize"] = 1e-5; }),
                      "description is invalid");
  expectIndexRejected(editedIndexBytes([](nlohmann::json& json) {
    const auto entry = json["palette"][0];

    while (json["palette"].size() <= terrain::maxPaletteSize)
    {
      json["palette"].push_back(entry);
    }
  }), "palette");
}

TEST(TerrainFormatTest, IndexWithABadByteCountFails)
{
  const auto setBytes = [](const nlohmann::json& value) {
    return editedIndexBytes([value](nlohmann::json& json) {
      regionEntry(json, nlohmann::json::array({ 0, 0, 0 }))["bytes"] = value;
    });
  };

  expectIndexRejected(setBytes(-1), "malformed");
  expectIndexRejected(setBytes(1.5), "malformed");
  expectIndexRejected(setBytes(terrain::regionHeaderBytes - 1), "impossible file size");
}

TEST(TerrainFormatTest, IndexWithTooManyRegionsFails)
{
  expectIndexRejected(editedIndexBytes([](nlohmann::json& json) {
    json["regions"] = nlohmann::json(std::vector<nlohmann::json>(terrain::maxRegions + 1));
  }), "too many regions");
}

TEST(TerrainFormatTest, IndexWithABadCoordinateFails)
{
  expectIndexRejected(editedIndexBytes([](nlohmann::json& json) {
    regionEntry(json, nlohmann::json::array({ 0, 0, 0 }))["chunks"][0]["coord"] =
      nlohmann::json::array({ static_cast<int64_t>(INT32_MIN) - 1, 0, 0 });
  }), "malformed");
  expectIndexRejected(editedIndexBytes([](nlohmann::json& json) {
    regionEntry(json, nlohmann::json::array({ 0, 0, 0 }))["chunks"][0]["coord"] = nlohmann::json::array({ 0.5, 0, 0 });
  }), "malformed");
  expectIndexRejected(editedIndexBytes([](nlohmann::json& json) {
    auto& chunks = regionEntry(json, nlohmann::json::array({ 0, 0, 0 }))["chunks"];
    chunks.push_back(chunks[0]);
  }), "repeated");
}

TEST(TerrainFormatTest, NestingIsAcceptedAtTheLimitAndRefusedPastIt)
{
  const auto withNesting = [](const int depth) {
    return editedIndexBytes([depth](nlohmann::json& json) { json["extra"] = nestedArray(depth); });
  };
  terrain::TerrainIndex parsed;
  std::string error;

  EXPECT_TRUE(terrain::parseIndex(withNesting(7), terrainUuid, parsed, error)) << error;
  expectIndexRejected(withNesting(8), "nests deeper");
}

TEST(TerrainFormatTest, EscapedQuotesAndBackslashesDoNotConfuseTheNestingScan)
{
  auto index = makeIndex();
  index.description.palette[0].name = "ends in a backslash\\";
  index.description.palette[1].name = "[[[[[[[[[[[[[[[[";
  terrain::TerrainIndex parsed;
  std::string error;

  ASSERT_TRUE(terrain::parseIndex(indexBytes(index), terrainUuid, parsed, error)) << error;
  EXPECT_EQ(parsed.description.palette[0].name, index.description.palette[0].name);
  EXPECT_EQ(parsed.description.palette[1].name, index.description.palette[1].name);
}

TEST(TerrainFormatTest, NameMatcherAcceptsOnlyNamesTheWriterProduces)
{
  EXPECT_TRUE(terrain::isRegionFileName("r.2097152.-2097152.0.0123456789abcdef.bin"));

  const char* refused[] = { "r.-0.0.0.0123456789abcdef.bin", "r.00.0.0.0123456789abcdef.bin",
                            "r.+1.0.0.0123456789abcdef.bin", "r.2097153.0.0.0123456789abcdef.bin",
                            "r.0.-2097153.0.0123456789abcdef.bin", "r.0.0.0.0123456789ABCDEF.bin" };

  for (const char* name : refused)
  {
    EXPECT_FALSE(terrain::isRegionFileName(name)) << name;
  }
}

TEST(TerrainFormatTest, FailedParseAndDecodeLeaveTheirOutputUntouched)
{
  terrain::TerrainIndex target;
  target.description = makeDescription(7.0f);
  target.regions[{ 5, 5, 5 }] = recordFor({ { { 40, 40, 40 }, solidChunk() } });

  terrain::TerrainIndex failed = target;
  std::string error;
  EXPECT_FALSE(terrain::parseIndex(editedIndexBytes([](nlohmann::json& json) {
    json["regions"].push_back(json["regions"][0]);
  }), terrainUuid, failed, error));
  EXPECT_EQ(failed.description.voxelSize, 7.0f);
  EXPECT_EQ(failed.description.palette.size(), 2u);
  ASSERT_EQ(failed.regions.size(), 1u);
  EXPECT_TRUE(failed.regions.contains({ 5, 5, 5 }));

  ASSERT_TRUE(terrain::parseIndex(indexBytes(), terrainUuid, target, error)) << error;
  EXPECT_EQ(target.description.voxelSize, 2.5f);
  EXPECT_EQ(target.regions.size(), 3u);
  EXPECT_FALSE(target.regions.contains({ 5, 5, 5 }));

  const auto chunks = twoChunks();
  auto record = recordFor(chunks);
  const auto bytes = terrain::encodeRegion(chunks).value();
  terrain::TerrainVolume::ChunkMap decoded;
  decoded.emplace(terrain::ChunkCoord{ 50, 50, 50 }, solidChunk());

  record.hashes[{ 1, 0, 0 }] ^= 1;
  EXPECT_FALSE(terrain::decodeRegion(bytes, { 0, 0, 0 }, record, decoded, error));
  EXPECT_EQ(decoded.size(), 1u);

  ASSERT_TRUE(terrain::decodeRegion(bytes, { 0, 0, 0 }, recordFor(chunks), decoded, error)) << error;
  EXPECT_EQ(decoded.size(), 3u);
}

TEST(TerrainFormatTest, DecodeRegionNamesEachEntryProblem)
{
  const auto dense = denseChunk(1);
  const terrain::HashByChunk hashes = { { { 0, 0, 0 }, dense->hash() } };

  expectRawRegionRejected({ rawEntry({ 0, 0, 0 }, dense), rawEntry({ 0, 0, 0 }, dense) }, hashes, "repeated");

  RawEntry tooLong = { { 0, 0, 0 }, std::vector<uint8_t>(terrain::maxEncodedChunkBytes + 1, 0), std::nullopt };
  expectRawRegionRejected({ tooLong }, hashes, "longer than the encoder");

  RawEntry pastEnd = rawEntry({ 0, 0, 0 }, dense);
  pastEnd.claimedLength = static_cast<uint32_t>(pastEnd.encoded.size() + 1);
  expectRawRegionRejected({ pastEnd }, hashes, "runs past the end");

  expectRawRegionRejected({ { { 0, 0, 0 }, { 0xFF, 0xFF, 0xFF }, std::nullopt } }, hashes, "does not decode");

  const auto air = terrain::TerrainChunk::uniform(terrain::airDistance, 0);
  expectRawRegionRejected({ rawEntry({ 0, 0, 0 }, air) }, { { { 0, 0, 0 }, air->hash() } }, "air chunk");

  expectRawRegionRejected({ rawEntry({ 0, 0, 0 }, dense) }, { { { 1, 0, 0 }, dense->hash() } }, "not in the index");

  const auto chunks = twoChunks();
  auto bytes = terrain::encodeRegion(chunks).value();
  bytes[7] = 1;
  expectRegionRejected(bytes, recordFor(chunks), "version");
}
