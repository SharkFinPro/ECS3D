#include "TerrainFiles.h"
#include "ProjectPaths.h"
#include "TerrainCodec.h"
#include "TerrainTypes.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace terrain
{
  namespace
  {
    using Json = nlohmann::json;
    using Key = std::array<int32_t, 3>;
    using HashByChunk = std::map<Key, uint64_t>;
    using RegionIndex = std::map<Key, HashByChunk>;
    using ChunksByRegion = std::map<Key, std::map<Key, std::shared_ptr<const TerrainChunk>>>;

    constexpr std::array<uint8_t, 4> regionMagic = { 'E', '3', 'T', 'R' };
    constexpr size_t regionHeaderBytes = 12;
    constexpr size_t entryHeaderBytes = 16;
    constexpr size_t maxRegionEntries = static_cast<size_t>(regionSize) * regionSize * regionSize;

    // Dense worst case: the codec splits each plane into literal runs of at most 128 samples, each costing one
    // control byte plus its values, so a chunk of incompressible data is the longest stream it can write.
    constexpr size_t maxLiteralRunSamples = 128;
    constexpr size_t literalRuns = static_cast<size_t>(samplesPerChunk) / maxLiteralRunSamples;
    constexpr size_t maxEncodedChunkBytes =
      1 + literalRuns * (1 + maxLiteralRunSamples * sizeof(int16_t)) + literalRuns * (1 + maxLiteralRunSamples);

    constexpr size_t maxRegionFileBytes =
      regionHeaderBytes + maxRegionEntries * (entryHeaderBytes + maxEncodedChunkBytes);
    constexpr uintmax_t maxIndexBytes = 64ull * 1024 * 1024;

    constexpr const char* indexFileName = "terrain.json";

    struct IndexData
    {
      TerrainDescription description;
      RegionIndex regions;
    };

    std::string failure(const std::filesystem::path& file, const std::string& what)
    {
      return pathToUtf8(file) + ": " + what;
    }

    Key regionOf(const Key& chunk)
    {
      return { floorDiv(chunk[0], regionSize), floorDiv(chunk[1], regionSize), floorDiv(chunk[2], regionSize) };
    }

    std::filesystem::path regionFile(const std::filesystem::path& folder, const Key& region)
    {
      return folder / ("r." + std::to_string(region[0]) + "." + std::to_string(region[1]) + "." +
                       std::to_string(region[2]) + ".bin");
    }

    std::string hashToString(const uint64_t hash)
    {
      char text[17];
      std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
      return text;
    }

    bool hashFromString(const std::string& text, uint64_t& hash)
    {
      if (text.size() != 16)
      {
        return false;
      }

      const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), hash, 16);

      return error == std::errc() && end == text.data() + text.size();
    }

    bool moveOnto(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& error)
    {
      std::filesystem::rename(from, to, error);

      if (!error)
      {
        return true;
      }

      // Windows refuses to rename over a file something else has open, so clear the destination and retry.
      std::error_code ignored;
      std::filesystem::remove(to, ignored);

      std::filesystem::rename(from, to, error);

      return !error;
    }

    bool writeFile(const std::filesystem::path& file, const std::span<const uint8_t> bytes, std::string& error)
    {
      auto temporary = file;
      temporary += ".tmp";

      {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.flush();

        if (!out)
        {
          out.close();
          std::error_code ignored;
          std::filesystem::remove(temporary, ignored);
          error = failure(temporary, "could not write the file");
          return false;
        }
      }

      std::error_code moveError;

      if (!moveOnto(temporary, file, moveError))
      {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        error = failure(file, "could not move the new file into place: " + moveError.message());
        return false;
      }

      return true;
    }

    bool readFile(const std::filesystem::path& file, const uintmax_t maxBytes, std::vector<uint8_t>& bytes,
                  std::string& error)
    {
      std::error_code sizeError;
      const uintmax_t size = std::filesystem::file_size(file, sizeError);

      if (sizeError)
      {
        error = failure(file, "could not read the file: " + sizeError.message());
        return false;
      }

      if (size > maxBytes)
      {
        error = failure(file, "the file is larger than the format allows");
        return false;
      }

      std::ifstream in(file, std::ios::binary);
      bytes.resize(static_cast<size_t>(size));
      in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));

      if (!in || in.gcount() != static_cast<std::streamsize>(size))
      {
        error = failure(file, "could not read the file");
        return false;
      }

      return true;
    }

    void putU32(std::vector<uint8_t>& out, const uint32_t value)
    {
      for (int shift = 0; shift < 32; shift += 8)
      {
        out.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
      }
    }

    uint32_t getU32(const std::vector<uint8_t>& bytes, const size_t at)
    {
      return static_cast<uint32_t>(bytes[at]) | (static_cast<uint32_t>(bytes[at + 1]) << 8) |
             (static_cast<uint32_t>(bytes[at + 2]) << 16) | (static_cast<uint32_t>(bytes[at + 3]) << 24);
    }

    bool readKey(const Json& value, Key& key)
    {
      if (!value.is_array() || value.size() != 3)
      {
        return false;
      }

      for (size_t i = 0; i < 3; ++i)
      {
        if (!value[i].is_number_integer())
        {
          return false;
        }

        const int64_t component = value[i].get<int64_t>();

        if (component < INT32_MIN || component > INT32_MAX)
        {
          return false;
        }

        key[i] = static_cast<int32_t>(component);
      }

      return true;
    }

    bool readUuid(const Json& value, uuids::uuid& uuid)
    {
      if (!value.is_string())
      {
        return false;
      }

      const auto parsed = uuids::uuid::from_string(value.get<std::string>());

      if (!parsed.has_value())
      {
        return false;
      }

      uuid = *parsed;
      return true;
    }

    bool readPalette(const Json& value, std::vector<TerrainMaterial>& palette)
    {
      if (!value.is_array() || value.size() > maxPaletteSize)
      {
        return false;
      }

      for (const Json& entry : value)
      {
        TerrainMaterial material;

        if (!entry.is_object() || !entry.contains("name") || !entry["name"].is_string() ||
            !entry.contains("albedoTexture") || !readUuid(entry["albedoTexture"], material.albedoTexture) ||
            !entry.contains("specularTexture") || !readUuid(entry["specularTexture"], material.specularTexture))
        {
          return false;
        }

        material.name = entry["name"].get<std::string>();
        palette.push_back(std::move(material));
      }

      return true;
    }

    bool readRegions(const Json& value, RegionIndex& regions, std::string& what)
    {
      if (!value.is_array())
      {
        what = "regions is not an array";
        return false;
      }

      for (const Json& entry : value)
      {
        Key region;

        if (!entry.is_object() || !entry.contains("region") || !readKey(entry["region"], region) ||
            !entry.contains("chunks") || !entry["chunks"].is_array())
        {
          what = "a region entry is malformed";
          return false;
        }

        const Json& chunks = entry["chunks"];

        if (chunks.empty() || chunks.size() > maxRegionEntries || regions.contains(region))
        {
          what = "a region entry is empty, oversized or repeated";
          return false;
        }

        HashByChunk& hashes = regions[region];

        for (const Json& chunk : chunks)
        {
          Key coord;
          uint64_t hash = 0;

          if (!chunk.is_object() || !chunk.contains("coord") || !readKey(chunk["coord"], coord) ||
              !chunk.contains("hash") || !chunk["hash"].is_string() ||
              !hashFromString(chunk["hash"].get<std::string>(), hash))
          {
            what = "a chunk entry is malformed";
            return false;
          }

          if (!isValidChunkCoord({ coord[0], coord[1], coord[2] }) || regionOf(coord) != region ||
              !hashes.emplace(coord, hash).second)
          {
            what = "a chunk entry is out of range, outside its region or repeated";
            return false;
          }
        }
      }

      return true;
    }

    bool readIndex(const std::filesystem::path& file, const uuids::uuid& uuid, IndexData& index, std::string& error)
    {
      std::vector<uint8_t> bytes;

      if (!readFile(file, maxIndexBytes, bytes, error))
      {
        return false;
      }

      const Json json = Json::parse(bytes.begin(), bytes.end(), nullptr, false);

      if (json.is_discarded() || !json.is_object())
      {
        error = failure(file, "the file is not a json object");
        return false;
      }

      uuids::uuid fileUuid;

      if (!json.contains("formatVersion") || !json["formatVersion"].is_number_integer() ||
          json["formatVersion"].get<int64_t>() != terrainFormatVersion)
      {
        error = failure(file, "unsupported or missing formatVersion");
        return false;
      }

      if (!json.contains("uuid") || !readUuid(json["uuid"], fileUuid) || fileUuid != uuid)
      {
        error = failure(file, "the uuid is missing or does not match the folder");
        return false;
      }

      if (!json.contains("voxelSize") || !json["voxelSize"].is_number())
      {
        error = failure(file, "voxelSize is missing or not a number");
        return false;
      }

      index.description.uuid = uuid;
      index.description.voxelSize = json["voxelSize"].get<float>();

      if (!json.contains("palette") || !readPalette(json["palette"], index.description.palette))
      {
        error = failure(file, "the palette is missing or malformed");
        return false;
      }

      if (!isValidDescription(index.description))
      {
        error = failure(file, "the description is invalid");
        return false;
      }

      std::string what;

      if (!json.contains("regions") || !readRegions(json["regions"], index.regions, what))
      {
        error = failure(file, what.empty() ? "regions is missing" : what);
        return false;
      }

      return true;
    }

    std::vector<uint8_t> encodeRegion(const std::map<Key, std::shared_ptr<const TerrainChunk>>& chunks)
    {
      std::vector<uint8_t> out(regionMagic.begin(), regionMagic.end());
      putU32(out, static_cast<uint32_t>(terrainFormatVersion));
      putU32(out, static_cast<uint32_t>(chunks.size()));

      for (const auto& [coord, chunk] : chunks)
      {
        const std::vector<uint8_t> encoded = encodeChunk(*chunk);

        for (const int32_t component : coord)
        {
          putU32(out, static_cast<uint32_t>(component));
        }

        putU32(out, static_cast<uint32_t>(encoded.size()));
        out.insert(out.end(), encoded.begin(), encoded.end());
      }

      return out;
    }

    bool decodeRegion(const std::filesystem::path& file, const Key& region, const HashByChunk& expected,
                      TerrainVolume::ChunkMap& chunks, std::string& error)
    {
      std::vector<uint8_t> bytes;

      if (!readFile(file, maxRegionFileBytes, bytes, error))
      {
        return false;
      }

      if (bytes.size() < regionHeaderBytes || !std::equal(regionMagic.begin(), regionMagic.end(), bytes.begin()))
      {
        error = failure(file, "bad magic or truncated header");
        return false;
      }

      if (getU32(bytes, 4) != static_cast<uint32_t>(terrainFormatVersion))
      {
        error = failure(file, "unsupported region format version");
        return false;
      }

      const uint32_t count = getU32(bytes, 8);

      if (count > maxRegionEntries)
      {
        error = failure(file, "too many entries");
        return false;
      }

      size_t position = regionHeaderBytes;
      std::set<Key> seen;

      for (uint32_t i = 0; i < count; ++i)
      {
        if (bytes.size() - position < entryHeaderBytes)
        {
          error = failure(file, "truncated entry header");
          return false;
        }

        const Key coord = { static_cast<int32_t>(getU32(bytes, position)),
                            static_cast<int32_t>(getU32(bytes, position + 4)),
                            static_cast<int32_t>(getU32(bytes, position + 8)) };
        const size_t length = getU32(bytes, position + 12);
        position += entryHeaderBytes;

        if (!isValidChunkCoord({ coord[0], coord[1], coord[2] }) || regionOf(coord) != region ||
            !seen.insert(coord).second)
        {
          error = failure(file, "an entry is out of range, outside this region or repeated");
          return false;
        }

        if (length > maxEncodedChunkBytes || length > bytes.size() - position)
        {
          error = failure(file, "an entry length is out of bounds");
          return false;
        }

        const auto chunk = decodeChunk(std::span<const uint8_t>(bytes).subspan(position, length));
        position += length;

        if (chunk == nullptr || chunk->isAir())
        {
          error = failure(file, "an entry does not decode to a chunk");
          return false;
        }

        const auto it = expected.find(coord);

        if (it == expected.end() || it->second != chunk->hash())
        {
          error = failure(file, "an entry is not in the index or its hash disagrees with it");
          return false;
        }

        chunks.emplace(ChunkCoord{ coord[0], coord[1], coord[2] }, chunk);
      }

      if (position != bytes.size())
      {
        error = failure(file, "trailing bytes after the last entry");
        return false;
      }

      if (seen.size() != expected.size())
      {
        error = failure(file, "the index lists chunks this file does not hold");
        return false;
      }

      return true;
    }

    bool isRegionFileName(const std::string& name)
    {
      return name.size() > 6 && name.starts_with("r.") && name.ends_with(".bin");
    }
  }

  std::filesystem::path terrainFolder(const std::filesystem::path& root, const uuids::uuid& uuid)
  {
    return root / "terrain" / uuids::to_string(uuid);
  }

  TerrainSaveResult saveTerrain(const std::filesystem::path& root, const TerrainDescription& description,
                                const TerrainVolume& volume)
  {
    TerrainSaveResult result;

    if (root.empty() || description.uuid.is_nil() || !isValidDescription(description))
    {
      result.error = "saveTerrain: the root is empty or the description is invalid";
      return result;
    }

    if (description.voxelSize != volume.voxelSize())
    {
      result.error = "saveTerrain: the description's voxelSize does not match the volume's";
      return result;
    }

    const std::filesystem::path folder = terrainFolder(root, description.uuid);
    std::error_code directoryError;
    std::filesystem::create_directories(folder, directoryError);

    if (directoryError)
    {
      result.error = failure(folder, "could not create the folder: " + directoryError.message());
      return result;
    }

    ChunksByRegion regions;
    RegionIndex newIndex;

    for (const auto& [coord, chunk] : volume.chunks())
    {
      const Key key = { coord.x, coord.y, coord.z };
      const Key region = regionOf(key);
      regions[region].emplace(key, chunk);
      newIndex[region].emplace(key, chunk->hash());
    }

    IndexData previous;
    std::string ignoredError;
    const bool havePrevious = readIndex(folder / indexFileName, description.uuid, previous, ignoredError);

    std::set<std::filesystem::path> keptFiles;

    for (const auto& [region, chunks] : regions)
    {
      const std::filesystem::path file = regionFile(folder, region);
      keptFiles.insert(file.filename());

      const auto old = havePrevious ? previous.regions.find(region) : previous.regions.end();

      if (old != previous.regions.end() && old->second == newIndex.at(region) && std::filesystem::exists(file))
      {
        continue;
      }

      if (!writeFile(file, encodeRegion(chunks), result.error))
      {
        return result;
      }

      ++result.regionsWritten;
    }

    Json json;
    json["formatVersion"] = terrainFormatVersion;
    json["uuid"] = uuids::to_string(description.uuid);
    json["voxelSize"] = description.voxelSize;
    json["palette"] = Json::array();

    for (const TerrainMaterial& material : description.palette)
    {
      json["palette"].push_back({ { "name", material.name },
                                  { "albedoTexture", uuids::to_string(material.albedoTexture) },
                                  { "specularTexture", uuids::to_string(material.specularTexture) } });
    }

    json["regions"] = Json::array();

    for (const auto& [region, hashes] : newIndex)
    {
      Json chunkList = Json::array();

      for (const auto& [coord, hash] : hashes)
      {
        chunkList.push_back({ { "coord", coord }, { "hash", hashToString(hash) } });
      }

      json["regions"].push_back({ { "region", region }, { "chunks", chunkList } });
    }

    const std::string text = json.dump(2);

    if (!writeFile(folder / indexFileName,
                   std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()), result.error))
    {
      return result;
    }

    std::vector<std::filesystem::path> obsolete;
    std::error_code listError;

    for (std::filesystem::directory_iterator it(folder, listError), end; !listError && it != end;
         it.increment(listError))
    {
      const std::filesystem::path name = it->path().filename();

      if (isRegionFileName(name.string()) && !keptFiles.contains(name))
      {
        obsolete.push_back(it->path());
      }
    }

    for (const std::filesystem::path& file : obsolete)
    {
      std::error_code removeError;

      if (std::filesystem::remove(file, removeError))
      {
        ++result.regionsRemoved;
      }
    }

    result.ok = true;
    return result;
  }

  TerrainLoadResult loadTerrain(const std::filesystem::path& root, const uuids::uuid& uuid)
  {
    TerrainLoadResult result;

    if (root.empty() || uuid.is_nil())
    {
      result.error = "loadTerrain: the root is empty or the uuid is nil";
      return result;
    }

    const std::filesystem::path folder = terrainFolder(root, uuid);
    IndexData index;

    if (!readIndex(folder / indexFileName, uuid, index, result.error))
    {
      return result;
    }

    TerrainVolume::ChunkMap chunks;

    for (const auto& [region, expected] : index.regions)
    {
      if (!decodeRegion(regionFile(folder, region), region, expected, chunks, result.error))
      {
        return result;
      }
    }

    index.description.chunkCount = static_cast<uint32_t>(chunks.size());

    auto volume = std::make_shared<TerrainVolume>(index.description.voxelSize);
    volume->restoreChunks(std::move(chunks));

    result.description = std::move(index.description);
    result.volume = std::move(volume);
    return result;
  }
}
