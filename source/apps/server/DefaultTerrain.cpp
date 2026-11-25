#include "DefaultTerrain.h"
#include "DefaultProjectParts.h"
#include "DefaultTerrainNoise.h"
#include "DefaultTerrainPaint.h"
#include "DefaultTerrainTiles.h"
#include <Log.h>
#include <terrain/TerrainBrush.h>
#include <terrain/TerrainOps.h>
#include <terrain/TerrainStore.h>
#include <terrain/TerrainTypes.h>
#include <terrain/TerrainVolume.h>
#include <glm/common.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <numbers>
#include <utility>
#include <vector>
#include <uuid.h>

using namespace defaultproject;
using namespace defaultterrain;
using nlohmann::json;

namespace {

constexpr float maxHeight = 126.0f;

// The ground height of a column, in voxels.
float groundAt(const float x, const float z)
{
  const float tunnelColumn = static_cast<float>(showcase::tunnelX);
  const float ridgeAxisZ = 160.0f + 10.0f * std::sin(x * 0.02f);
  const float beside = (z - ridgeAxisZ) / 26.0f;
  const float massif = 1.0f + 0.3f * std::exp(-std::pow((x - tunnelColumn) / 80.0f, 2.0f));
  const float modulation = 1.0f + 0.1f * valueNoise(x / 40.0f, 0.5f, 4) *
                                    (1.0f - std::exp(-std::pow((x - tunnelColumn) / 50.0f, 2.0f)));
  const float ridge = 62.0f * massif * modulation * std::exp(-beside * beside);

  // The hills stay flat where a feature stands on or opens onto the ground.
  const float tunnelCorridor = (1.0f - easeBetween(10.0f, 40.0f, std::abs(x - tunnelColumn))) *
                               (1.0f - easeBetween(70.0f, 100.0f, std::abs(z - 160.0f)));
  const float calm =
    (1.0f - tunnelCorridor) *
    easeBetween(25.0f, 60.0f,
                distance2D(x, z, static_cast<float>(showcase::archX), static_cast<float>(showcase::archZ))) *
    easeBetween(8.0f, 40.0f, distance2D(x, z, 205.0f, static_cast<float>(showcase::overhangZ)));
  const float hillAmplitude = 6.0f + 14.0f * easeBetween(50.0f, 110.0f, std::abs(z - 160.0f));
  float height = static_cast<float>(showcase::groundHeight) + hillAmplitude * fbm(x, z) * calm + ridge;

  // A mesa whose west and south walls are steep.
  const float mesa = easeBetween(222.0f, 228.0f, x) * easeBetween(225.0f, 231.0f, z);
  height = glm::mix(height, 84.0f + 3.0f * fbm(x + 500.0f, z), mesa);

  return std::clamp(height, 4.0f, maxHeight - 1.0f);
}

struct Ground
{
  std::vector<float> heights;

  Ground()
    : heights(static_cast<size_t>(showcase::columns) * static_cast<size_t>(showcase::columns))
  {
    for (int z = 0; z < showcase::columns; ++z)
    {
      for (int x = 0; x < showcase::columns; ++x)
      {
        heights[static_cast<size_t>(z) * showcase::columns + static_cast<size_t>(x)] =
          groundAt(static_cast<float>(x), static_cast<float>(z));
      }
    }
  }

  [[nodiscard]] float at(const int x, const int z) const
  {
    return heights[static_cast<size_t>(std::clamp(z, 0, showcase::columns - 1)) * showcase::columns +
                   static_cast<size_t>(std::clamp(x, 0, showcase::columns - 1))];
  }
};

void fillGround(terrain::TerrainVolume& volume, const Ground& ground)
{
  fillHeights(volume, ground.heights, showcase::columns, maxHeight, groundThickness, interiorMaterial);
}

void applyAndCommit(terrain::TerrainVolume& volume, const terrain::BrushOp::Kind kind, const float x, const float y,
                    const float z, const float radius)
{
  const float voxelSize = volume.voxelSize();
  terrain::BrushOp op;
  op.kind = kind;
  op.center = glm::vec3(x, y, z) * voxelSize;
  op.radius = radius * voxelSize;
  op.strength = 1.0f;
  op.falloff = 0.0f;
  op.material = interiorMaterial;
  terrain::commit(volume, terrain::applyBrush(volume, op));
}

void carveTunnel(terrain::TerrainVolume& volume)
{
  const float spacing = static_cast<float>(showcase::tunnelRadius) * 0.5f;

  for (float z = static_cast<float>(showcase::tunnelStartZ); z <= static_cast<float>(showcase::tunnelEndZ);
       z += spacing)
  {
    applyAndCommit(volume, terrain::BrushOp::Kind::subtract, static_cast<float>(showcase::tunnelX),
                   static_cast<float>(showcase::tunnelY), z, static_cast<float>(showcase::tunnelRadius));
  }
}

void raiseArch(terrain::TerrainVolume& volume)
{
  constexpr int steps = 16;

  for (int step = 0; step <= steps; ++step)
  {
    const float angle = std::numbers::pi_v<float> * static_cast<float>(step) / static_cast<float>(steps);
    applyAndCommit(volume, terrain::BrushOp::Kind::add,
                   static_cast<float>(showcase::archX) + static_cast<float>(showcase::archRingRadius) * std::cos(angle),
                   static_cast<float>(showcase::groundHeight) +
                     static_cast<float>(showcase::archRingRadius) * std::sin(angle),
                   static_cast<float>(showcase::archZ), static_cast<float>(showcase::archTubeRadius));
  }
}

void raiseOverhang(terrain::TerrainVolume& volume)
{
  for (int column = 0; column < 4; ++column)
  {
    for (int row = -1; row <= 1; ++row)
    {
      applyAndCommit(volume, terrain::BrushOp::Kind::add, static_cast<float>(showcase::overhangX + 6 * (column - 1)),
                     static_cast<float>(showcase::overhangY), static_cast<float>(showcase::overhangZ + 10 * row), 7.0f);
    }
  }
}

void floatIsland(terrain::TerrainVolume& volume)
{
  struct Lump
  {
    float x;
    float y;
    float z;
    float radius;
  };

  const Lump lumps[] = {
    { 0.0f, 0.0f, 0.0f, 14.0f },
    { 12.0f, 2.0f, -5.0f, 10.0f },
    { -12.0f, -2.0f, 5.0f, 9.0f },
    { 0.0f, -14.0f, 0.0f, 7.0f },
    { 3.0f, -20.0f, 1.0f, 4.0f }
  };

  for (const auto& lump : lumps)
  {
    applyAndCommit(volume, terrain::BrushOp::Kind::add, static_cast<float>(showcase::islandX) + lump.x,
                   static_cast<float>(showcase::islandY) + lump.y, static_cast<float>(showcase::islandZ) + lump.z,
                   lump.radius);
  }
}

bool inIsland(const float x, const float y, const float z)
{
  return y >= 85.0f &&
         distance2D(x, z, static_cast<float>(showcase::islandX), static_cast<float>(showcase::islandZ)) < 40.0f;
}

bool inOverhang(const glm::ivec3 sample)
{
  return sample.x < 226 && sample.x > 190 && sample.z > 230 && sample.z < 280 && sample.y >= 60;
}

bool inArch(const float x, const float y, const float z)
{
  return std::abs(x - static_cast<float>(showcase::archX)) <= 32.0f &&
         std::abs(z - static_cast<float>(showcase::archZ)) <= 14.0f &&
         y > static_cast<float>(showcase::groundHeight) + 1.0f;
}

uint8_t rockMaterial(const float normalY)
{
  return normalY > 0.6f ? materialGray : materialBrown;
}

uint8_t groundMaterial(const glm::ivec3 sample, const float normalY, const Ground& ground)
{
  const float height = ground.at(sample.x, sample.z);

  if (height - static_cast<float>(sample.y) > 3.0f || normalY < 0.6f)
  {
    return materialBrown;
  }

  if (height > 88.0f)
  {
    return materialGray;
  }

  return height < static_cast<float>(showcase::groundHeight) + 1.0f ? materialSand : materialGrass;
}

uint8_t materialFor(const glm::ivec3 sample, const float normalY, const Ground& ground)
{
  const float x = static_cast<float>(sample.x);
  const float y = static_cast<float>(sample.y);
  const float z = static_cast<float>(sample.z);

  if (inIsland(x, y, z))
  {
    return normalY > 0.4f ? materialGrass : materialBrown;
  }

  if (inOverhang(sample) || inArch(x, y, z))
  {
    return rockMaterial(normalY);
  }

  return groundMaterial(sample, normalY, ground);
}

}

void generateShowcaseGround(terrain::TerrainVolume& volume)
{
  fillGround(volume, Ground());
}

void generateShowcaseTerrain(terrain::TerrainVolume& volume)
{
  const Ground ground;

  fillGround(volume, ground);
  carveTunnel(volume);
  raiseArch(volume);
  raiseOverhang(volume);
  floatIsland(volume);
  paintSurface(volume, showcase::columns, [&ground](const glm::ivec3 sample, const float normalY)
  {
    return materialFor(sample, normalY, ground);
  });
}

namespace {

void installShowcaseTerrain(terrain::TerrainStore& store)
{
  const auto uuid = uuids::uuid::from_string(showcaseTerrain);
  if (!uuid)
  {
    return;
  }

  const auto* current = store.description(*uuid);
  if (!current)
  {
    return;
  }

  auto description = *current;
  description.voxelSize = showcase::voxelSize;
  if (!store.applyDescription(description))
  {
    Log::warn(LogCategory::server, "The built-in terrain's voxel size was refused; it was not generated");
    return;
  }

  const auto volume = store.find(*uuid);
  generateShowcaseTerrain(*volume);

  description.chunkCount = static_cast<uint32_t>(volume->chunks().size());
  store.applyDescription(description);
  store.stampAllChunks(*uuid);
}

}

void installDefaultTerrains(terrain::TerrainStore& store)
{
  installShowcaseTerrain(store);
  installBasinTerrains(store);
}

namespace defaultproject {

json buildTerrainShowcase()
{
  return json::array({
    makeObject("Terrain", json::array({
      transform(showcase::terrainOrigin()),
      terrainCollider(showcaseTerrain)
    })),
    player(showcase::playerSpawn(), 0, glm::vec3(0.0f, 0.0f, -1.0f), 180.0f),
    daySun(250.0f)
  });
}

}
