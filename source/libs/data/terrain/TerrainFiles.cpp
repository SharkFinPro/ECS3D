#include "TerrainFiles.h"
#include "ProjectFingerprint.h"
#include "ProjectPaths.h"
#include "TerrainCodec.h"
#include "TerrainTypes.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cfloat>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace terrain
{
  namespace
  {
    using Json = nlohmann::json;
    using Key = std::array<int32_t, 3>;
    using HashByChunk = std::map<Key, uint64_t>;

    struct RegionRecord
    {
      HashByChunk hashes;
      uint64_t tag = 0;
      uintmax_t bytes = 0;
    };

    using RegionIndex = std::map<Key, RegionRecord>;
    using ChunksByRegion = std::map<Key, std::map<Key, std::shared_ptr<const TerrainChunk>>>;

    constexpr std::array<uint8_t, 4> regionMagic = { 'E', '3', 'T', 'R' };
    constexpr size_t regionHeaderBytes = 12;
    constexpr size_t entryHeaderBytes = 16;
    constexpr size_t maxRegionEntries = static_cast<size_t>(regionSize) * regionSize * regionSize;

    constexpr size_t maxRegionFileBytes =
      regionHeaderBytes + maxRegionEntries * (entryHeaderBytes + maxEncodedChunkBytes);
    constexpr uintmax_t maxIndexBytes = 8ull * 1024 * 1024;
    constexpr size_t maxRegions = 65536;
    constexpr int maxIndexDepth = 8;
    constexpr int moveAttempts = 3;
    constexpr auto moveRetryDelay = std::chrono::milliseconds(20);

    constexpr const char* indexFileName = "terrain.json";
    constexpr const char* temporarySuffix = ".tmp";

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

    std::string hashToString(const uint64_t hash)
    {
      char text[17];
      std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
      return text;
    }

    std::string regionFileName(const Key& region, const uint64_t tag)
    {
      return "r." + std::to_string(region[0]) + "." + std::to_string(region[1]) + "." + std::to_string(region[2]) +
             "." + hashToString(tag) + ".bin";
    }

    std::vector<std::string> splitOnDots(const std::string& text)
    {
      std::vector<std::string> parts(1);

      for (const char c : text)
      {
        if (c == '.')
        {
          parts.emplace_back();
        }
        else
        {
          parts.back().push_back(c);
        }
      }

      return parts;
    }

    bool isLowercaseHex16(const std::string& text)
    {
      return text.size() == 16 && std::all_of(text.begin(), text.end(), [](const char c)
      {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
      });
    }

    bool isSignedInteger(const std::string& text)
    {
      int32_t value = 0;
      const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);

      return !text.empty() && error == std::errc() && end == text.data() + text.size();
    }

    // Only names this format writes are ever deleted, so a foreign file in the folder is left alone.
    bool isRegionFileName(const std::string& name)
    {
      const std::vector<std::string> parts = splitOnDots(name);

      return parts.size() == 6 && parts[0] == "r" && isSignedInteger(parts[1]) && isSignedInteger(parts[2]) &&
             isSignedInteger(parts[3]) && isLowercaseHex16(parts[4]) && parts[5] == "bin";
    }

    bool isOwnTemporaryName(const std::string& name)
    {
      const std::string suffix = temporarySuffix;

      if (!name.ends_with(suffix))
      {
        return false;
      }

      const std::string base = name.substr(0, name.size() - suffix.size());
      return base == indexFileName || isRegionFileName(base);
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

    // Windows can refuse a rename while another process briefly holds a file, so a few short retries cover it.
    bool moveOnto(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& error)
    {
      for (int attempt = 0; attempt < moveAttempts; ++attempt)
      {
        std::filesystem::rename(from, to, error);

        if (!error)
        {
          return true;
        }

        if (attempt + 1 < moveAttempts)
        {
          std::this_thread::sleep_for(moveRetryDelay);
        }
      }

      return false;
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

    // Names the exact chunk set a region file holds, so a file is never reused for different content.
    uint64_t regionTag(const HashByChunk& hashes)
    {
      std::vector<uint8_t> bytes;

      for (const auto& [coord, hash] : hashes)
      {
        for (const int32_t component : coord)
        {
          putU32(bytes, static_cast<uint32_t>(component));
        }

        putU32(bytes, static_cast<uint32_t>(hash & 0xFFFFFFFFu));
        putU32(bytes, static_cast<uint32_t>(hash >> 32));
      }

      return fingerprintBytes(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    }

    bool readKey(const Json& value, Key& key)
    {
      if (!value.is_array() || value.size() != 3)
      {
        return false;
      }

      for (size_t i = 0; i < 3; ++i)
      {
        if (!value[i].is_number_integer() ||
            (value[i].is_number_unsigned() && value[i].get<uint64_t>() > static_cast<uint64_t>(INT32_MAX)))
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

    bool readByteCount(const Json& value, uintmax_t& count)
    {
      if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<int64_t>() < 0))
      {
        return false;
      }

      count = value.get<uint64_t>();
      return true;
    }

    // Nesting is bounded before parsing because the parser recurses once per level.
    bool nestsWithin(const std::vector<uint8_t>& bytes, const int maxDepth)
    {
      int depth = 0;
      bool inString = false;
      bool escaped = false;

      for (const uint8_t byte : bytes)
      {
        if (inString)
        {
          if (escaped)
          {
            escaped = false;
          }
          else if (byte == '\\')
          {
            escaped = true;
          }
          else if (byte == '"')
          {
            inString = false;
          }

          continue;
        }

        if (byte == '"')
        {
          inString = true;
        }
        else if ((byte == '[' || byte == '{') && ++depth > maxDepth)
        {
          return false;
        }
        else if ((byte == ']' || byte == '}') && depth > 0)
        {
          --depth;
        }
      }

      return true;
    }

    // The shortest decimal text that reads back as the same float, so a stored size shows as 0.1 and not 0.10000000149.
    double shortestFloatAsDouble(const float value)
    {
      char text[32];

      for (int digits = 1; digits <= 9; ++digits)
      {
        std::snprintf(text, sizeof(text), "%.*g", digits, static_cast<double>(value));

        if (std::strtof(text, nullptr) == value)
        {
          break;
        }
      }

      return std::strtod(text, nullptr);
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
      if (!value.is_array() || value.size() > maxRegions)
      {
        what = "regions is not an array or lists too many regions";
        return false;
      }

      for (const Json& entry : value)
      {
        Key region;
        RegionRecord record;

        if (!entry.is_object() || !entry.contains("region") || !readKey(entry["region"], region) ||
            !entry.contains("tag") || !entry["tag"].is_string() ||
            !hashFromString(entry["tag"].get<std::string>(), record.tag) || !entry.contains("bytes") ||
            !readByteCount(entry["bytes"], record.bytes) || !entry.contains("chunks") || !entry["chunks"].is_array())
        {
          what = "a region entry is malformed";
          return false;
        }

        const Json& chunks = entry["chunks"];

        if (chunks.empty() || chunks.size() > maxRegionEntries || regions.contains(region) ||
            record.bytes < regionHeaderBytes || record.bytes > maxRegionFileBytes)
        {
          what = "a region entry is empty, oversized, repeated or has an impossible file size";
          return false;
        }

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
              !record.hashes.emplace(coord, hash).second)
          {
            what = "a chunk entry is out of range, outside its region or repeated";
            return false;
          }
        }

        regions.emplace(region, std::move(record));
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

      if (!nestsWithin(bytes, maxIndexDepth))
      {
        error = failure(file, "the file nests deeper than the format allows");
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

      const double voxelSize = json["voxelSize"].get<double>();

      if (!(std::fabs(voxelSize) <= static_cast<double>(FLT_MAX)))
      {
        error = failure(file, "voxelSize is outside the range of a float");
        return false;
      }

      index.description.uuid = uuid;
      index.description.voxelSize = static_cast<float>(voxelSize);

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

    bool decodeRegion(const std::filesystem::path& file, const Key& region, const RegionRecord& record,
                      TerrainVolume::ChunkMap& chunks, std::string& error)
    {
      std::vector<uint8_t> bytes;

      if (!readFile(file, maxRegionFileBytes, bytes, error))
      {
        return false;
      }

      const HashByChunk& expected = record.hashes;

      if (bytes.size() != record.bytes)
      {
        error = failure(file, "the file size disagrees with the index");
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

    void removeLeftoverTemporaries(const std::filesystem::path& folder)
    {
      std::vector<std::filesystem::path> leftovers;
      std::error_code listError;

      for (std::filesystem::directory_iterator it(folder, listError), end; !listError && it != end;
           it.increment(listError))
      {
        if (isOwnTemporaryName(pathToUtf8(it->path().filename())))
        {
          leftovers.push_back(it->path());
        }
      }

      for (const std::filesystem::path& file : leftovers)
      {
        std::error_code removeError;
        std::filesystem::remove(file, removeError);
      }
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

    removeLeftoverTemporaries(folder);

    ChunksByRegion regions;
    RegionIndex newIndex;

    for (const auto& [coord, chunk] : volume.chunks())
    {
      const Key key = { coord.x, coord.y, coord.z };
      const Key region = regionOf(key);
      regions[region].emplace(key, chunk);
      newIndex[region].hashes.emplace(key, chunk->hash());
    }

    IndexData previous;
    std::string ignoredError;
    const bool havePrevious = readIndex(folder / indexFileName, description.uuid, previous, ignoredError);

    std::set<std::string> keptNames;

    for (auto& [region, record] : newIndex)
    {
      record.tag = regionTag(record.hashes);

      const std::string name = regionFileName(region, record.tag);
      const std::filesystem::path file = folder / name;
      keptNames.insert(name);

      const auto old = havePrevious ? previous.regions.find(region) : previous.regions.end();
      const bool sizeKnown = old != previous.regions.end() && old->second.tag == record.tag;

      std::vector<uint8_t> encoded;
      uintmax_t expectedBytes = 0;

      if (sizeKnown)
      {
        expectedBytes = old->second.bytes;
      }
      else
      {
        encoded = encodeRegion(regions.at(region));
        expectedBytes = encoded.size();
      }

      std::error_code sizeError;
      const uintmax_t onDisk = std::filesystem::file_size(file, sizeError);

      if (sizeError || onDisk != expectedBytes)
      {
        if (encoded.empty())
        {
          encoded = encodeRegion(regions.at(region));
        }

        if (!writeFile(file, encoded, result.error))
        {
          return result;
        }

        expectedBytes = encoded.size();
        ++result.regionsWritten;
      }

      record.bytes = expectedBytes;
    }

    Json json;
    json["formatVersion"] = terrainFormatVersion;
    json["uuid"] = uuids::to_string(description.uuid);
    json["voxelSize"] = shortestFloatAsDouble(description.voxelSize);
    json["palette"] = Json::array();

    for (const TerrainMaterial& material : description.palette)
    {
      json["palette"].push_back({ { "name", material.name },
                                  { "albedoTexture", uuids::to_string(material.albedoTexture) },
                                  { "specularTexture", uuids::to_string(material.specularTexture) } });
    }

    json["regions"] = Json::array();

    for (const auto& [region, record] : newIndex)
    {
      Json chunkList = Json::array();

      for (const auto& [coord, hash] : record.hashes)
      {
        chunkList.push_back({ { "coord", coord }, { "hash", hashToString(hash) } });
      }

      json["regions"].push_back({ { "region", region },
                                  { "tag", hashToString(record.tag) },
                                  { "bytes", record.bytes },
                                  { "chunks", chunkList } });
    }

    const std::string text = json.dump(2);

    if (text.size() > maxIndexBytes)
    {
      result.error = failure(folder / indexFileName, "the index would be larger than the format allows");
      return result;
    }

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
      const std::string name = pathToUtf8(it->path().filename());

      if (isRegionFileName(name) && !keptNames.contains(name))
      {
        obsolete.push_back(it->path());
      }
    }

    for (const std::filesystem::path& file : obsolete)
    {
      std::error_code removeError;

      if (std::filesystem::remove(file, removeError) && !removeError)
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

    for (const auto& [region, record] : index.regions)
    {
      if (!decodeRegion(folder / regionFileName(region, record.tag), region, record, chunks, result.error))
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
