#include "TerrainFiles.h"
#include "ProjectPaths.h"
#include "assets/AssetRegistry.h"
#include "TerrainFormat.h"
#include "TerrainTypes.h"
#include <algorithm>
#include <chrono>
#include <exception>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace terrain
{
  namespace
  {
    using ChunksByRegion = std::map<ChunkKey, ChunksByKey>;

    constexpr int moveAttempts = 3;
    constexpr auto moveRetryDelay = std::chrono::milliseconds(20);

    constexpr const char* temporarySuffix = ".tmp";

    std::string failure(const std::filesystem::path& file, const std::string& what)
    {
      return pathToUtf8(file) + ": " + what;
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
      temporary += temporarySuffix;

      std::string problem;

      {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);

        if (!out)
        {
          problem = "could not open the temporary file for writing";
        }
        else
        {
          out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
          out.flush();

          if (!out)
          {
            problem = "could not write the temporary file";
          }
          else
          {
            out.close();

            if (out.fail())
            {
              problem = "could not close the temporary file";
            }
          }
        }
      }

      if (!problem.empty())
      {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        error = failure(temporary, problem);
        return false;
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

    bool readIndex(const std::filesystem::path& file, const uuids::uuid& uuid, TerrainIndex& index, std::string& error)
    {
      std::vector<uint8_t> bytes;

      if (!readFile(file, maxIndexBytes, bytes, error))
      {
        return false;
      }

      std::string what;

      if (!parseIndex(bytes, uuid, index, what))
      {
        error = failure(file, what);
        return false;
      }

      return true;
    }

    bool readRegion(const std::filesystem::path& file, const ChunkKey& region, const RegionRecord& record,
                    TerrainVolume::ChunkMap& chunks, std::string& error)
    {
      std::vector<uint8_t> bytes;

      if (!readFile(file, maxRegionFileBytes, bytes, error))
      {
        return false;
      }

      std::string what;

      if (!decodeRegion(bytes, region, record, chunks, what))
      {
        error = failure(file, what);
        return false;
      }

      return true;
    }

    std::vector<uuids::uuid> terrainRecords(const AssetRegistry& registry)
    {
      std::vector<uuids::uuid> ordered;

      for (const auto& [uuid, record] : registry.getAssets())
      {
        if (record.type == AssetType::Terrain)
        {
          ordered.push_back(uuid);
        }
      }

      std::sort(ordered.begin(), ordered.end());
      return ordered;
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

    if (root.empty())
    {
      result.error = "saveTerrain: the root is empty";
      return result;
    }

    if (description.uuid.is_nil())
    {
      result.error = "saveTerrain: the description's uuid is nil";
      return result;
    }

    if (!isValidDescription(description))
    {
      result.error = "saveTerrain: the description is invalid";
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
    TerrainIndex newIndex;
    newIndex.description = description;

    for (const auto& [coord, chunk] : volume.chunks())
    {
      const ChunkKey key = { coord.x, coord.y, coord.z };
      const ChunkKey region = regionOfChunk(key);
      regions[region].emplace(key, chunk);
      newIndex.regions[region].hashes.emplace(key, chunk->hash());
    }

    TerrainIndex previous;
    std::string ignoredError;
    const bool havePrevious = readIndex(folder / indexFileName, description.uuid, previous, ignoredError);

    std::set<std::string> keptNames;

    for (auto& [region, record] : newIndex.regions)
    {
      record.tag = regionTag(record.hashes);

      const std::string name = regionFileName(region, record.tag);
      const std::filesystem::path file = folder / name;
      keptNames.insert(name);

      const auto old = havePrevious ? previous.regions.find(region) : previous.regions.end();
      const bool expectationKnown = old != previous.regions.end() && old->second.tag == record.tag;

      std::optional<std::vector<uint8_t>> encoded;
      uintmax_t expectedBytes = 0;
      uint64_t expectedHash = 0;

      if (expectationKnown)
      {
        expectedBytes = old->second.bytes;
        expectedHash = old->second.fileHash;
      }
      else
      {
        encoded = encodeRegion(regions.at(region));

        if (!encoded.has_value())
        {
          result.error = failure(file, "the region could not be encoded");
          return result;
        }

        expectedBytes = encoded->size();
        expectedHash = fileFingerprint(*encoded);
      }

      std::error_code sizeError;
      const uintmax_t onDisk = std::filesystem::file_size(file, sizeError);
      bool intact = !sizeError && onDisk == expectedBytes;

      if (intact)
      {
        std::vector<uint8_t> existing;
        std::string ignoredRead;
        intact = readFile(file, maxRegionFileBytes, existing, ignoredRead) && fileFingerprint(existing) == expectedHash;
      }

      if (!intact)
      {
        if (!encoded.has_value())
        {
          encoded = encodeRegion(regions.at(region));

          if (!encoded.has_value())
          {
            result.error = failure(file, "the region could not be encoded");
            return result;
          }
        }

        if (!writeFile(file, *encoded, result.error))
        {
          return result;
        }

        expectedBytes = encoded->size();
        expectedHash = fileFingerprint(*encoded);
        ++result.regionsWritten;
      }

      record.bytes = expectedBytes;
      record.fileHash = expectedHash;
    }

    const std::optional<std::string> index = writeIndex(newIndex);

    if (!index.has_value())
    {
      result.error = failure(folder / indexFileName, "the index would be invalid or larger than the format allows");
      return result;
    }

    const std::string& text = *index;

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

    if (root.empty())
    {
      result.error = "loadTerrain: the root is empty";
      return result;
    }

    if (uuid.is_nil())
    {
      result.error = "loadTerrain: the uuid is nil";
      return result;
    }

    const std::filesystem::path folder = terrainFolder(root, uuid);
    TerrainIndex index;

    if (!readIndex(folder / indexFileName, uuid, index, result.error))
    {
      return result;
    }

    TerrainVolume::ChunkMap chunks;

    for (const auto& [region, record] : index.regions)
    {
      if (!readRegion(folder / regionFileName(region, record.tag), region, record, chunks, result.error))
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

  TerrainLoadReport loadProjectTerrains(const std::filesystem::path& root, const AssetRegistry& registry,
                                        TerrainStore& store)
  {
    TerrainLoadReport report;

    if (root.empty())
    {
      return report;
    }

    for (const uuids::uuid& uuid : terrainRecords(registry))
    {
      try
      {
        std::error_code existsError;

        const auto indexFile = terrainFolder(root, uuid) / indexFileName;

        if (!std::filesystem::exists(indexFile, existsError))
        {
          if (existsError && existsError != std::errc::no_such_file_or_directory &&
              existsError != std::errc::not_a_directory)
          {
            report.errors.push_back(failure(terrainFolder(root, uuid), "could not check for the index: " +
                                                                         existsError.message()));
          }
          else
          {
            ++report.missing;
          }

          continue;
        }

        TerrainLoadResult loaded = loadTerrain(root, uuid);

        if (loaded.description.has_value() && store.adopt(*loaded.description, std::move(loaded.volume)))
        {
          ++report.loaded;
        }
        else if (!loaded.error.empty())
        {
          report.errors.push_back(std::move(loaded.error));
        }
        else
        {
          report.errors.push_back(failure(terrainFolder(root, uuid) / indexFileName, "the terrain was refused"));
        }
      }
      catch (const std::exception& e)
      {
        report.errors.push_back(failure(terrainFolder(root, uuid), e.what()));
      }
    }

    return report;
  }

  TerrainSaveReport saveProjectTerrains(const std::filesystem::path& root, const AssetRegistry& registry,
                                        const TerrainStore& store)
  {
    TerrainSaveReport report;

    if (root.empty())
    {
      return report;
    }

    for (const uuids::uuid& uuid : terrainRecords(registry))
    {
      const TerrainDescription* description = store.description(uuid);
      const std::shared_ptr<TerrainVolume> volume = store.find(uuid);

      if (description == nullptr || volume == nullptr)
      {
        continue;
      }

      try
      {
        TerrainSaveResult saved = saveTerrain(root, *description, *volume);

        if (saved.ok)
        {
          ++report.saved;
        }
        else
        {
          report.errors.push_back(std::move(saved.error));
        }
      }
      catch (const std::exception& e)
      {
        report.errors.push_back(failure(terrainFolder(root, uuid), e.what()));
      }
    }

    return report;
  }
}
