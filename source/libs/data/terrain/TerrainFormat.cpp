#include "TerrainFormat.h"
#include "ProjectFingerprint.h"
#include "TerrainTypes.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cfloat>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>

namespace terrain
{
  namespace
  {
    using Json = nlohmann::json;

    constexpr std::array<uint8_t, 4> regionMagic = { 'E', '3', 'T', 'R' };
    constexpr int maxIndexDepth = 8;

    std::string hashToString(const uint64_t hash)
    {
      char text[17];
      std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
      return text;
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

    bool int32FromString(const std::string& text, int32_t& value)
    {
      const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);

      return !text.empty() && error == std::errc() && end == text.data() + text.size();
    }

    bool isValidRegion(const ChunkKey& region)
    {
      constexpr int32_t maxRegionCoord = maxChunkCoord / regionSize;

      return std::ranges::all_of(region, [](const int32_t component) {
        return component >= -maxRegionCoord && component <= maxRegionCoord;
      });
    }

    bool hashFromString(const std::string& text, uint64_t& hash)
    {
      if (!isLowercaseHex16(text))
      {
        return false;
      }

      const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), hash, 16);

      return error == std::errc() && end == text.data() + text.size();
    }

    void putU32(std::vector<uint8_t>& out, const uint32_t value)
    {
      for (int shift = 0; shift < 32; shift += 8)
      {
        out.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
      }
    }

    uint32_t getU32(const std::span<const uint8_t> bytes, const size_t at)
    {
      return static_cast<uint32_t>(bytes[at]) | (static_cast<uint32_t>(bytes[at + 1]) << 8) |
             (static_cast<uint32_t>(bytes[at + 2]) << 16) | (static_cast<uint32_t>(bytes[at + 3]) << 24);
    }

    bool readKey(const Json& value, ChunkKey& key)
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
    bool nestsWithin(const std::span<const uint8_t> bytes, const int maxDepth)
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

        if (static_cast<float>(std::strtod(text, nullptr)) == value)
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
      if (!value.is_array())
      {
        what = "regions is not an array";
        return false;
      }

      if (value.size() > maxRegions)
      {
        what = "regions lists too many regions";
        return false;
      }

      for (const Json& entry : value)
      {
        ChunkKey region;
        RegionRecord record;

        if (!entry.is_object() || !entry.contains("region") || !readKey(entry["region"], region) ||
            !entry.contains("tag") || !entry["tag"].is_string() ||
            !hashFromString(entry["tag"].get<std::string>(), record.tag) || !entry.contains("bytes") ||
            !readByteCount(entry["bytes"], record.bytes) || !entry.contains("fileHash") ||
            !entry["fileHash"].is_string() || !hashFromString(entry["fileHash"].get<std::string>(), record.fileHash) ||
            !entry.contains("chunks") || !entry["chunks"].is_array())
        {
          what = "a region entry is malformed";
          return false;
        }

        const Json& chunks = entry["chunks"];

        if (chunks.empty())
        {
          what = "a region entry is empty";
          return false;
        }

        if (chunks.size() > maxRegionEntries)
        {
          what = "a region entry is oversized";
          return false;
        }

        if (regions.contains(region))
        {
          what = "a region entry is repeated";
          return false;
        }

        if (record.bytes < regionHeaderBytes || record.bytes > maxRegionFileBytes)
        {
          what = "a region entry has an impossible file size";
          return false;
        }

        for (const Json& chunk : chunks)
        {
          ChunkKey coord;
          uint64_t hash = 0;

          if (!chunk.is_object() || !chunk.contains("coord") || !readKey(chunk["coord"], coord) ||
              !chunk.contains("hash") || !chunk["hash"].is_string() ||
              !hashFromString(chunk["hash"].get<std::string>(), hash))
          {
            what = "a chunk entry is malformed";
            return false;
          }

          if (!isValidChunkCoord({ coord[0], coord[1], coord[2] }) || regionOfChunk(coord) != region)
          {
            what = "a chunk entry is out of range or outside its region";
            return false;
          }

          if (!record.hashes.emplace(coord, hash).second)
          {
            what = "a chunk entry is repeated";
            return false;
          }
        }

        if (record.tag != regionTag(record.hashes))
        {
          what = "a region tag does not match its chunk hashes";
          return false;
        }

        regions.emplace(region, std::move(record));
      }

      return true;
    }
  }

  ChunkKey regionOfChunk(const ChunkKey& chunk)
  {
    return { floorDiv(chunk[0], regionSize), floorDiv(chunk[1], regionSize), floorDiv(chunk[2], regionSize) };
  }

  uint64_t regionTag(const HashByChunk& hashes)
  {
    std::vector<uint8_t> bytes;
    putU32(bytes, codecVersion);

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

  uint64_t fileFingerprint(const std::span<const uint8_t> bytes)
  {
    return fingerprintBytes(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
  }

  std::string regionFileName(const ChunkKey& region, const uint64_t tag)
  {
    return "r." + std::to_string(region[0]) + "." + std::to_string(region[1]) + "." + std::to_string(region[2]) + "." +
           hashToString(tag) + ".bin";
  }

  bool isRegionFileName(const std::string& name)
  {
    const std::vector<std::string> parts = splitOnDots(name);

    ChunkKey region;
    uint64_t tag = 0;

    return parts.size() == 6 && parts[0] == "r" && int32FromString(parts[1], region[0]) &&
           int32FromString(parts[2], region[1]) && int32FromString(parts[3], region[2]) && isValidRegion(region) &&
           hashFromString(parts[4], tag) && regionFileName(region, tag) == name;
  }

  std::optional<std::vector<uint8_t>> encodeRegion(const ChunksByKey& chunks)
  {
    if (chunks.empty() || chunks.size() > maxRegionEntries)
    {
      return std::nullopt;
    }

    const ChunkKey region = regionOfChunk(chunks.begin()->first);

    for (const auto& [coord, chunk] : chunks)
    {
      if (chunk == nullptr || chunk->isAir() || !isValidChunkCoord({ coord[0], coord[1], coord[2] }) ||
          regionOfChunk(coord) != region)
      {
        return std::nullopt;
      }
    }

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

  bool decodeRegion(const std::span<const uint8_t> bytes, const ChunkKey& region, const RegionRecord& record,
                    TerrainVolume::ChunkMap& chunks, std::string& error)
  {
    const HashByChunk& expected = record.hashes;

    if (bytes.size() != record.bytes)
    {
      error = "the file size disagrees with the index";
      return false;
    }

    if (fileFingerprint(bytes) != record.fileHash)
    {
      error = "the file hash disagrees with the index";
      return false;
    }

    if (bytes.size() < regionHeaderBytes || !std::equal(regionMagic.begin(), regionMagic.end(), bytes.begin()))
    {
      error = "bad magic or truncated header";
      return false;
    }

    if (getU32(bytes, 4) != static_cast<uint32_t>(terrainFormatVersion))
    {
      error = "unsupported region format version";
      return false;
    }

    const uint32_t count = getU32(bytes, 8);

    if (count > maxRegionEntries)
    {
      error = "too many entries";
      return false;
    }

    size_t position = regionHeaderBytes;
    std::set<ChunkKey> seen;
    TerrainVolume::ChunkMap decoded;

    for (uint32_t i = 0; i < count; ++i)
    {
      if (bytes.size() - position < entryHeaderBytes)
      {
        error = "truncated entry header";
        return false;
      }

      const ChunkKey coord = { static_cast<int32_t>(getU32(bytes, position)),
                               static_cast<int32_t>(getU32(bytes, position + 4)),
                               static_cast<int32_t>(getU32(bytes, position + 8)) };
      const size_t length = getU32(bytes, position + 12);
      position += entryHeaderBytes;

      if (!isValidChunkCoord({ coord[0], coord[1], coord[2] }) || regionOfChunk(coord) != region)
      {
        error = "an entry is out of range or outside this region";
        return false;
      }

      if (!seen.insert(coord).second)
      {
        error = "an entry is repeated";
        return false;
      }

      if (length > maxEncodedChunkBytes)
      {
        error = "an entry is longer than the encoder can write";
        return false;
      }

      if (length > bytes.size() - position)
      {
        error = "an entry runs past the end of the file";
        return false;
      }

      const auto chunk = decodeChunk(bytes.subspan(position, length));
      position += length;

      if (chunk == nullptr)
      {
        error = "an entry does not decode";
        return false;
      }

      if (chunk->isAir())
      {
        error = "an entry is an air chunk";
        return false;
      }

      const auto it = expected.find(coord);

      if (it == expected.end())
      {
        error = "an entry is not in the index";
        return false;
      }

      if (it->second != chunk->hash())
      {
        error = "an entry hash disagrees with the index";
        return false;
      }

      decoded.emplace(ChunkCoord{ coord[0], coord[1], coord[2] }, chunk);
    }

    if (position != bytes.size())
    {
      error = "trailing bytes after the last entry";
      return false;
    }

    if (seen.size() != expected.size())
    {
      error = "the index lists chunks this file does not hold";
      return false;
    }

    chunks.merge(decoded);
    return true;
  }

  std::optional<std::string> writeIndex(const TerrainIndex& index)
  {
    if (!isValidDescription(index.description))
    {
      return std::nullopt;
    }

    Json json;
    json["formatVersion"] = terrainFormatVersion;
    json["uuid"] = uuids::to_string(index.description.uuid);
    json["voxelSize"] = shortestFloatAsDouble(index.description.voxelSize);
    json["palette"] = Json::array();

    for (const TerrainMaterial& material : index.description.palette)
    {
      json["palette"].push_back({ { "name", material.name },
                                  { "albedoTexture", uuids::to_string(material.albedoTexture) },
                                  { "specularTexture", uuids::to_string(material.specularTexture) } });
    }

    json["regions"] = Json::array();

    for (const auto& [region, record] : index.regions)
    {
      Json chunkList = Json::array();

      for (const auto& [coord, hash] : record.hashes)
      {
        chunkList.push_back({ { "coord", coord }, { "hash", hashToString(hash) } });
      }

      json["regions"].push_back({ { "region", region },
                                  { "tag", hashToString(record.tag) },
                                  { "bytes", record.bytes },
                                  { "fileHash", hashToString(record.fileHash) },
                                  { "chunks", chunkList } });
    }

    std::string text = json.dump(2);
    TerrainIndex reread;
    std::string error;

    // Parsing the result back is what keeps the writer from emitting anything the reader refuses.
    if (!parseIndex({ reinterpret_cast<const uint8_t*>(text.data()), text.size() }, index.description.uuid, reread,
                    error))
    {
      return std::nullopt;
    }

    return text;
  }

  bool parseIndex(const std::span<const uint8_t> text, const uuids::uuid& expectedUuid, TerrainIndex& index,
                  std::string& error)
  {
    if (text.size() > maxIndexBytes)
    {
      error = "the file is larger than the format allows";
      return false;
    }

    if (!nestsWithin(text, maxIndexDepth))
    {
      error = "the file nests deeper than the format allows";
      return false;
    }

    const Json json = Json::parse(text.begin(), text.end(), nullptr, false);

    if (json.is_discarded() || !json.is_object())
    {
      error = "the file is not a json object";
      return false;
    }

    uuids::uuid fileUuid;

    if (!json.contains("formatVersion") || !json["formatVersion"].is_number_integer() ||
        json["formatVersion"].get<int64_t>() != terrainFormatVersion)
    {
      error = "unsupported or missing formatVersion";
      return false;
    }

    if (!json.contains("uuid") || !readUuid(json["uuid"], fileUuid) || fileUuid != expectedUuid)
    {
      error = "the uuid is missing or does not match the folder";
      return false;
    }

    if (!json.contains("voxelSize") || !json["voxelSize"].is_number())
    {
      error = "voxelSize is missing or not a number";
      return false;
    }

    const double voxelSize = json["voxelSize"].get<double>();

    if (!(std::fabs(voxelSize) <= static_cast<double>(FLT_MAX)))
    {
      error = "voxelSize is outside the range of a float";
      return false;
    }

    TerrainIndex parsed;
    parsed.description.uuid = expectedUuid;
    parsed.description.voxelSize = static_cast<float>(voxelSize);

    if (!json.contains("palette") || !readPalette(json["palette"], parsed.description.palette))
    {
      error = "the palette is missing or malformed";
      return false;
    }

    if (!isValidDescription(parsed.description))
    {
      error = "the description is invalid";
      return false;
    }

    std::string what;

    if (!json.contains("regions") || !readRegions(json["regions"], parsed.regions, what))
    {
      error = what.empty() ? "regions is missing" : what;
      return false;
    }

    index = std::move(parsed);
    return true;
  }
}
