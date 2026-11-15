#include "TerrainFormatFixtures.h"

using namespace terrainFormatFixtures;

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
    EXPECT_EQ(parsed.regions.at(region).fileHash, record.fileHash);
    EXPECT_NE(record.fileHash, 0u);
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

TEST(TerrainFormatTest, IndexWithABadFileHashFails)
{
  const char* bad[] = { "xyz", "0123456789ABCDEF", "0123456789abcde", "+123456789abcdef", "" };

  for (const char* text : bad)
  {
    expectIndexRejected(editedIndexBytes([text](nlohmann::json& json) {
      regionEntry(json, nlohmann::json::array({ 0, 0, 0 }))["fileHash"] = text;
    }), "malformed");
  }

  expectIndexRejected(editedIndexBytes([](nlohmann::json& json) {
    regionEntry(json, nlohmann::json::array({ 0, 0, 0 })).erase("fileHash");
  }), "malformed");
  expectIndexRejected(editedIndexBytes([](nlohmann::json& json) {
    regionEntry(json, nlohmann::json::array({ 0, 0, 0 }))["fileHash"] = 5;
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
