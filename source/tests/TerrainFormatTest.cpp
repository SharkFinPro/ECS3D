#include <gtest/gtest.h>

#include "terrain/TerrainChunk.h"
#include "terrain/TerrainFormat.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
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
    record.bytes = terrain::encodeRegion(chunks).size();
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
    return bytesOf(terrain::writeIndex(index));
  }

  std::vector<uint8_t> editedIndexBytes(const std::function<void(nlohmann::json&)>& edit)
  {
    auto json = nlohmann::json::parse(terrain::writeIndex(makeIndex()));
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
    const auto goodBytes = terrain::encodeRegion(chunks);
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
  const auto bytes = terrain::encodeRegion(chunks);
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
  auto bytes = terrain::encodeRegion(chunks);
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
  auto bytes = terrain::encodeRegion(chunks);
  bytes[0] = 'X';

  expectRegionRejected(bytes, recordFor(chunks), "magic");
}

TEST(TerrainFormatTest, UnsupportedRegionVersionFails)
{
  const auto chunks = twoChunks();
  auto bytes = terrain::encodeRegion(chunks);
  bytes[4] = static_cast<uint8_t>(terrain::terrainFormatVersion + 1);

  expectRegionRejected(bytes, recordFor(chunks), "version");
}

TEST(TerrainFormatTest, HashDisagreeingWithIndexFails)
{
  const auto chunks = twoChunks();
  auto record = recordFor(chunks);
  record.hashes[{ 1, 0, 0 }] = 0x0123456789abcdefull;

  expectRegionRejected(terrain::encodeRegion(chunks), record, "hash disagrees");
}

TEST(TerrainFormatTest, IndexListingAChunkTheBytesLackFails)
{
  const auto chunks = twoChunks();
  auto record = recordFor(chunks);
  record.hashes[{ 2, 0, 0 }] = 0x0123456789abcdefull;

  expectRegionRejected(terrain::encodeRegion(chunks), record, "does not hold");
}

TEST(TerrainFormatTest, TrailingBytesFail)
{
  const auto chunks = twoChunks();
  auto bytes = terrain::encodeRegion(chunks);
  auto record = recordFor(chunks);
  bytes.push_back(0);

  expectRegionRejected(bytes, record, "disagrees with the index");

  record.bytes = bytes.size();
  expectRegionRejected(bytes, record, "trailing");
}

TEST(TerrainFormatTest, EntryCountOverTheMaximumFails)
{
  const auto chunks = twoChunks();
  auto bytes = terrain::encodeRegion(chunks);
  const uint32_t tooMany = static_cast<uint32_t>(terrain::maxRegionEntries) + 1;
  bytes[8] = static_cast<uint8_t>(tooMany & 0xFF);
  bytes[9] = static_cast<uint8_t>(tooMany >> 8);

  expectRegionRejected(bytes, recordFor(chunks), "too many entries");
}

TEST(TerrainFormatTest, EntryCoordOutsideItsRegionFails)
{
  const auto chunks = twoChunks();
  auto bytes = terrain::encodeRegion(chunks);
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
    const std::string text = terrain::writeIndex(makeIndex(size));

    ASSERT_TRUE(terrain::parseIndex(bytesOf(text), terrainUuid, parsed, error)) << size << ": " << error;
    EXPECT_EQ(parsed.description.voxelSize, size);
    EXPECT_EQ(static_cast<float>(nlohmann::json::parse(text)["voxelSize"].get<double>()), size);
  }

  EXPECT_EQ(nlohmann::json::parse(terrain::writeIndex(makeIndex(0.1f)))["voxelSize"].get<double>(), 0.1);
}
