#ifndef TERRAINFORMATFIXTURES_H
#define TERRAINFORMATFIXTURES_H

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
#include <uuid.h>

// Shared by the terrain format suites: sample chunks, a sample index, and hand-built region bytes.
namespace terrainFormatFixtures {
  inline const uuids::uuid terrainUuid = uuids::uuid::from_string("0A1B2C3D-0000-4000-8000-00000000ABCD").value();

  inline std::shared_ptr<const terrain::TerrainChunk> denseChunk(const uint32_t seed)
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

  inline std::shared_ptr<const terrain::TerrainChunk> solidChunk()
  {
    return terrain::TerrainChunk::uniform(-terrain::airDistance, 1);
  }

  inline terrain::TerrainDescription makeDescription(const float voxelSize = 2.5f)
  {
    terrain::TerrainDescription description;
    description.uuid = terrainUuid;
    description.voxelSize = voxelSize;
    description.palette.push_back({ "grass", terrainUuid, uuids::uuid() });
    description.palette.push_back({ "rock", uuids::uuid(), terrainUuid });
    return description;
  }

  // Region (0,0,0) holds two chunks, so a region with several entries is covered.
  inline terrain::ChunksByKey twoChunks()
  {
    return { { { 0, 0, 0 }, denseChunk(1) }, { { 1, 0, 0 }, solidChunk() } };
  }

  inline terrain::RegionRecord recordFor(const terrain::ChunksByKey& chunks)
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
  inline terrain::TerrainIndex makeIndex(const float voxelSize = 2.5f)
  {
    terrain::TerrainIndex index;
    index.description = makeDescription(voxelSize);
    index.regions[{ 0, 0, 0 }] = recordFor(twoChunks());
    index.regions[{ -1, -1, -1 }] = recordFor({ { { -1, -1, -1 }, denseChunk(2) } });
    index.regions[{ 1, 0, 0 }] = recordFor({ { { 9, 0, 0 }, denseChunk(3) } });
    return index;
  }

  inline std::vector<uint8_t> bytesOf(const std::string& text)
  {
    return { text.begin(), text.end() };
  }

  inline std::vector<uint8_t> indexBytes(const terrain::TerrainIndex& index = makeIndex())
  {
    return bytesOf(terrain::writeIndex(index).value());
  }

  inline std::vector<uint8_t> editedIndexBytes(const std::function<void(nlohmann::json&)>& edit)
  {
    auto json = nlohmann::json::parse(terrain::writeIndex(makeIndex()).value());
    edit(json);
    return bytesOf(json.dump());
  }

  inline nlohmann::json& regionEntry(nlohmann::json& index, const nlohmann::json& key)
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
  inline void expectIndexRejected(const std::vector<uint8_t>& text, const char* mention)
  {
    terrain::TerrainIndex parsed;
    std::string error;
    ASSERT_TRUE(terrain::parseIndex(indexBytes(), terrainUuid, parsed, error)) << error;

    terrain::TerrainIndex rejected;
    error.clear();
    EXPECT_FALSE(terrain::parseIndex(text, terrainUuid, rejected, error));
    EXPECT_NE(error.find(mention), std::string::npos) << error;
  }

  inline void expectRegionRejected(const std::vector<uint8_t>& bytes, const terrain::RegionRecord& record, const char* mention)
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

  using terrain::ChunkKey;

  inline nlohmann::json nestedArray(const int depth)
  {
    nlohmann::json value = nlohmann::json::array();

    for (int i = 1; i < depth; ++i)
    {
      value = nlohmann::json::array({ value });
    }

    return value;
  }

  inline void putU32(std::vector<uint8_t>& out, const uint32_t value)
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
  inline std::vector<uint8_t> rawRegion(const std::vector<RawEntry>& entries)
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

  inline void expectRawRegionRejected(const std::vector<RawEntry>& entries, const terrain::HashByChunk& hashes,
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

  inline RawEntry rawEntry(const ChunkKey& coord, const std::shared_ptr<const terrain::TerrainChunk>& chunk)
  {
    return { coord, terrain::encodeChunk(*chunk), std::nullopt };
  }
}

#endif //TERRAINFORMATFIXTURES_H
