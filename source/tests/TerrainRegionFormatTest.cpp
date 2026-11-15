#include "TerrainFormatFixtures.h"

using namespace terrainFormatFixtures;

TEST(TerrainRegionFormatTest, FileHashMismatchIsRefusedWithItsOwnMessage)
{
  const auto chunks = twoChunks();
  const auto bytes = terrain::encodeRegion(chunks).value();
  auto record = recordFor(chunks);
  terrain::TerrainVolume::ChunkMap decoded;
  std::string error;

  ASSERT_TRUE(terrain::decodeRegion(bytes, { 0, 0, 0 }, record, decoded, error)) << error;

  record.fileHash ^= 1;
  decoded.clear();
  EXPECT_FALSE(terrain::decodeRegion(bytes, { 0, 0, 0 }, record, decoded, error));
  EXPECT_NE(error.find("file hash"), std::string::npos) << error;
  EXPECT_TRUE(decoded.empty());
}

TEST(TerrainRegionFormatTest, RegionOfChunkUsesFloorDivision)
{
  EXPECT_EQ(terrain::regionOfChunk({ 0, 0, 0 }), (terrain::ChunkKey{ 0, 0, 0 }));
  EXPECT_EQ(terrain::regionOfChunk({ 7, 8, -8 }), (terrain::ChunkKey{ 0, 1, -1 }));
  EXPECT_EQ(terrain::regionOfChunk({ -1, -1, -1 }), (terrain::ChunkKey{ -1, -1, -1 }));
  EXPECT_EQ(terrain::regionOfChunk({ -9, 2, 3 }), (terrain::ChunkKey{ -2, 0, 0 }));
}

TEST(TerrainRegionFormatTest, FileNameCarriesCoordinatesAndSixteenHexTag)
{
  const std::string name = terrain::regionFileName({ -1, 2, 30 }, 0xABCDEF);
  EXPECT_EQ(name, "r.-1.2.30.0000000000abcdef.bin");
  EXPECT_TRUE(terrain::isRegionFileName(name));
}

TEST(TerrainRegionFormatTest, NameMatcherAcceptsOwnNamesAndRejectsForeignOnes)
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

TEST(TerrainRegionFormatTest, TagIsStableAndTracksTheChunkSet)
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

TEST(TerrainRegionFormatTest, RegionBytesRoundTrip)
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

TEST(TerrainRegionFormatTest, TruncatedRegionBytesFail)
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

TEST(TerrainRegionFormatTest, GarbledMagicFails)
{
  const auto chunks = twoChunks();
  auto bytes = terrain::encodeRegion(chunks).value();
  bytes[0] = 'X';

  expectRegionRejected(bytes, recordFor(chunks), "magic");
}

TEST(TerrainRegionFormatTest, UnsupportedRegionVersionFails)
{
  const auto chunks = twoChunks();
  auto bytes = terrain::encodeRegion(chunks).value();
  bytes[4] = static_cast<uint8_t>(terrain::terrainFormatVersion + 1);

  expectRegionRejected(bytes, recordFor(chunks), "version");
}

TEST(TerrainRegionFormatTest, HashDisagreeingWithIndexFails)
{
  const auto chunks = twoChunks();
  auto record = recordFor(chunks);
  record.hashes[{ 1, 0, 0 }] = 0x0123456789abcdefull;

  expectRegionRejected(terrain::encodeRegion(chunks).value(), record, "hash disagrees");
}

TEST(TerrainRegionFormatTest, IndexListingAChunkTheBytesLackFails)
{
  const auto chunks = twoChunks();
  auto record = recordFor(chunks);
  record.hashes[{ 2, 0, 0 }] = 0x0123456789abcdefull;

  expectRegionRejected(terrain::encodeRegion(chunks).value(), record, "does not hold");
}

TEST(TerrainRegionFormatTest, TrailingBytesFail)
{
  const auto chunks = twoChunks();
  auto bytes = terrain::encodeRegion(chunks).value();
  auto record = recordFor(chunks);
  bytes.push_back(0);

  expectRegionRejected(bytes, record, "disagrees with the index");

  record.bytes = bytes.size();
  expectRegionRejected(bytes, record, "trailing");
}

TEST(TerrainRegionFormatTest, EntryCountOverTheMaximumFails)
{
  const auto chunks = twoChunks();
  auto bytes = terrain::encodeRegion(chunks).value();
  const uint32_t tooMany = static_cast<uint32_t>(terrain::maxRegionEntries) + 1;
  bytes[8] = static_cast<uint8_t>(tooMany & 0xFF);
  bytes[9] = static_cast<uint8_t>(tooMany >> 8);

  expectRegionRejected(bytes, recordFor(chunks), "too many entries");
}

TEST(TerrainRegionFormatTest, EntryCoordOutsideItsRegionFails)
{
  const auto chunks = twoChunks();
  auto bytes = terrain::encodeRegion(chunks).value();
  bytes[terrain::regionHeaderBytes] = static_cast<uint8_t>(terrain::regionSize);

  expectRegionRejected(bytes, recordFor(chunks), "outside this region");
}

TEST(TerrainRegionFormatTest, EncodeRegionRefusesWhatDecodeRegionCouldNotRead)
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

TEST(TerrainRegionFormatTest, RegionBytesRoundTripWithNegativeCoordinates)
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

TEST(TerrainRegionFormatTest, CodecVersionAndByteOrderArePinned)
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

TEST(TerrainRegionFormatTest, NameMatcherAcceptsOnlyNamesTheWriterProduces)
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

TEST(TerrainRegionFormatTest, DecodeRegionNamesEachEntryProblem)
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
