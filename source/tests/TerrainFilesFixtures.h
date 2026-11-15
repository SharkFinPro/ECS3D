#ifndef TERRAINFILESFIXTURES_H
#define TERRAINFILESFIXTURES_H

#include <gtest/gtest.h>
#include "ProjectPaths.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainFiles.h"
#include "terrain/TerrainFormat.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>
#include <uuid.h>

// Shared by the terrain file suites: a sample volume and a temporary project root per test.
namespace terrainFilesFixtures {
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

  // Five chunks in four regions: (0,0,0) holds two, (-1,-1,-1), (1,0,0) and (-2,0,0) one each.
  inline std::shared_ptr<terrain::TerrainVolume> makeVolume(const float voxelSize = 2.5f)
  {
    auto volume = std::make_shared<terrain::TerrainVolume>(voxelSize);
    volume->setChunk({ 0, 0, 0 }, denseChunk(1));
    volume->setChunk({ 1, 0, 0 }, solidChunk());
    volume->setChunk({ -1, -1, -1 }, denseChunk(2));
    volume->setChunk({ 9, 0, 0 }, denseChunk(3));
    volume->setChunk({ -9, 2, 3 }, solidChunk());
    return volume;
  }

  inline std::vector<uint8_t> readBytes(const std::filesystem::path& file)
  {
    std::ifstream in(file, std::ios::binary);
    return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
  }

  inline void writeBytes(const std::filesystem::path& file, const std::vector<uint8_t>& bytes)
  {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }

  inline void garbleKeepingSize(const std::filesystem::path& file)
  {
    auto bytes = readBytes(file);
    ASSERT_GT(bytes.size(), 40u);
    bytes[bytes.size() / 2] ^= 0xFF;
    writeBytes(file, bytes);
  }

  inline void expectErrorMentions(const terrain::TerrainLoadResult& loaded, const char* text)
  {
    EXPECT_FALSE(loaded.description.has_value());
    EXPECT_EQ(loaded.volume, nullptr);
    EXPECT_NE(loaded.error.find(text), std::string::npos) << loaded.error;
  }

  class TerrainFilesTest : public testing::Test
  {
  protected:
    void SetUp() override
    {
      const auto* info = testing::UnitTest::GetInstance()->current_test_info();
      std::random_device device;
      m_root = std::filesystem::temp_directory_path() /
               (std::string("ecs3d_terrain_files_") + info->name() + "_" + std::to_string(device()));
      std::filesystem::remove_all(m_root);
      std::filesystem::create_directories(m_root);
    }

    void TearDown() override
    {
      std::error_code ignored;
      std::filesystem::remove_all(m_root, ignored);
    }

    [[nodiscard]] std::filesystem::path folder() const
    {
      return terrain::terrainFolder(m_root, terrainUuid);
    }

    [[nodiscard]] std::filesystem::path region(const char* name) const
    {
      return folder() / name;
    }

    // The region file whose name starts with prefix, such as "r.1.0.0.", or an empty path when there is none.
    [[nodiscard]] std::filesystem::path regionFile(const std::string& prefix) const
    {
      for (const auto& entry : std::filesystem::directory_iterator(folder()))
      {
        const std::string name = pathToUtf8(entry.path().filename());

        if (name.starts_with(prefix) && name.ends_with(".bin"))
        {
          return entry.path();
        }
      }

      return {};
    }

    [[nodiscard]] static std::vector<std::string> fileNames(const std::filesystem::path& directory)
    {
      std::vector<std::string> names;

      for (const auto& entry : std::filesystem::directory_iterator(directory))
      {
        names.push_back(pathToUtf8(entry.path().filename()));
      }

      std::sort(names.begin(), names.end());
      return names;
    }

    void saveDefault()
    {
      const auto result = terrain::saveTerrain(m_root, makeDescription(), *makeVolume());
      ASSERT_TRUE(result.ok) << result.error;
    }

    void expectLoadFails(const std::filesystem::path& namedFile)
    {
      const auto loaded = terrain::loadTerrain(m_root, terrainUuid);
      EXPECT_FALSE(loaded.description.has_value());
      EXPECT_EQ(loaded.volume, nullptr);
      EXPECT_FALSE(loaded.error.empty());
      EXPECT_NE(loaded.error.find(pathToUtf8(namedFile)), std::string::npos) << loaded.error;
    }

    void editIndex(const std::function<void(nlohmann::json&)>& edit)
    {
      const auto file = folder() / "terrain.json";
      auto json = nlohmann::json::parse(readBytes(file));
      edit(json);
      const std::string text = json.dump();
      writeBytes(file, std::vector<uint8_t>(text.begin(), text.end()));
    }

    std::filesystem::path m_root;
  };
}

#endif //TERRAINFILESFIXTURES_H
