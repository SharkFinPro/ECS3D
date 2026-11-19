#include "DefaultTerrain.h"
#include "DefaultProjectParts.h"
#include <Log.h>
#include <terrain/TerrainBrush.h>
#include <terrain/TerrainChunk.h>
#include <terrain/TerrainGenerators.h>
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
#include <memory>
#include <numbers>
#include <tuple>
#include <utility>
#include <vector>
#include <uuid.h>

using namespace defaultproject;
using nlohmann::json;

namespace {

constexpr float maxHeight = 126.0f;
constexpr float groundThickness = 8.0f;
constexpr uint8_t interiorMaterial = 2;

constexpr uint8_t materialGray = 0;
constexpr uint8_t materialGrass = 1;
constexpr uint8_t materialBrown = 2;
constexpr uint8_t materialSand = 4;

uint32_t hashCell(const int x, const int z, const uint32_t seed)
{
  uint32_t value = static_cast<uint32_t>(x) * 0x8DA6B343u ^ static_cast<uint32_t>(z) * 0xD8163841u ^ seed * 0xCB1AB31Fu;
  value ^= value >> 15;
  value *= 0x2C1B3C6Du;
  value ^= value >> 12;
  value *= 0x297A2D39u;
  value ^= value >> 15;

  return value;
}

float easeCurve(const float t)
{
  return t * t * (3.0f - 2.0f * t);
}

float easeBetween(const float low, const float high, const float value)
{
  return easeCurve(std::clamp((value - low) / (high - low), 0.0f, 1.0f));
}

// In [-1, 1].
float valueNoise(const float x, const float z, const uint32_t seed)
{
  const int cellX = static_cast<int>(std::floor(x));
  const int cellZ = static_cast<int>(std::floor(z));
  const float fractionX = easeCurve(x - static_cast<float>(cellX));
  const float fractionZ = easeCurve(z - static_cast<float>(cellZ));

  const auto corner = [seed](const int cx, const int cz)
  {
    return static_cast<float>(hashCell(cx, cz, seed) & 0xFFFFu) / 32767.5f - 1.0f;
  };

  const float lower = glm::mix(corner(cellX, cellZ), corner(cellX + 1, cellZ), fractionX);
  const float upper = glm::mix(corner(cellX, cellZ + 1), corner(cellX + 1, cellZ + 1), fractionX);

  return glm::mix(lower, upper, fractionZ);
}

float fbm(const float x, const float z)
{
  return (valueNoise(x / 64.0f, z / 64.0f, 1) + 0.5f * valueNoise(x / 32.0f, z / 32.0f, 2) +
          0.25f * valueNoise(x / 16.0f, z / 16.0f, 3)) / 1.75f;
}

float distance2D(const float x, const float z, const float centerX, const float centerZ)
{
  return std::hypot(x - centerX, z - centerZ);
}

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
  std::vector<uint16_t> pixels(ground.heights.size());
  for (size_t index = 0; index < pixels.size(); ++index)
  {
    pixels[index] = static_cast<uint16_t>(std::lround(ground.heights[index] / maxHeight * 65535.0f));
  }

  const terrain::HeightmapView view{ pixels, showcase::columns, showcase::columns };
  terrain::commit(volume, terrain::fillFromHeightmap(volume, view, maxHeight * volume.voxelSize(),
                                                     groundThickness * volume.voxelSize(), interiorMaterial));
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

std::vector<terrain::ChunkCoord> denseChunks(const terrain::TerrainVolume& volume)
{
  std::vector<terrain::ChunkCoord> dense;
  for (const auto& [coord, chunk] : volume.chunks())
  {
    if (!chunk->isUniform())
    {
      dense.push_back(coord);
    }
  }

  std::ranges::sort(dense, [](const terrain::ChunkCoord& a, const terrain::ChunkCoord& b)
  {
    return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z);
  });

  return dense;
}

// One chunk's distances for the slope reads; a neighbor outside the chunk comes from the volume. Reads are clamped
// to the ground's columns so the outermost samples do not see the world beyond the box as air.
class SlopeReader
{
public:
  SlopeReader(const terrain::TerrainVolume& volume, const terrain::ChunkCoord coord,
              const std::vector<int16_t>& distances)
    : m_volume(volume), m_origin(terrain::chunkOrigin(coord)), m_distances(distances)
  {}

  [[nodiscard]] float normalY(const glm::ivec3 sample) const
  {
    const float gradX = static_cast<float>(distanceAt(sample + glm::ivec3(1, 0, 0)) -
                                           distanceAt(sample - glm::ivec3(1, 0, 0)));
    const float gradY = static_cast<float>(distanceAt(sample + glm::ivec3(0, 1, 0)) -
                                           distanceAt(sample - glm::ivec3(0, 1, 0)));
    const float gradZ = static_cast<float>(distanceAt(sample + glm::ivec3(0, 0, 1)) -
                                           distanceAt(sample - glm::ivec3(0, 0, 1)));
    const float length = std::sqrt(gradX * gradX + gradY * gradY + gradZ * gradZ);

    return length > 0.0f ? gradY / length : 1.0f;
  }

private:
  [[nodiscard]] int distanceAt(glm::ivec3 sample) const
  {
    sample.x = std::clamp(sample.x, 0, showcase::columns - 1);
    sample.z = std::clamp(sample.z, 0, showcase::columns - 1);
    const glm::ivec3 local = sample - m_origin;
    const bool inside = local.x >= 0 && local.y >= 0 && local.z >= 0 && local.x < terrain::chunkSize &&
                        local.y < terrain::chunkSize && local.z < terrain::chunkSize;

    return static_cast<int>(inside ? m_distances[static_cast<size_t>(terrain::sampleIndex(local.x, local.y, local.z))]
                                   : m_volume.distanceAtSample(sample));
  }

  const terrain::TerrainVolume& m_volume;
  glm::ivec3 m_origin;
  const std::vector<int16_t>& m_distances;
};

bool isSurfaceSample(const int16_t distance)
{
  return distance < terrain::airDistance && distance > -terrain::airDistance;
}

// The chunk repainted, or null when no material changed.
std::shared_ptr<const terrain::TerrainChunk> paintChunk(const terrain::TerrainVolume& volume,
                                                        const terrain::ChunkCoord coord, const Ground& ground)
{
  const auto source = volume.chunk(coord);
  const glm::ivec3 origin = terrain::chunkOrigin(coord);
  const std::vector<int16_t> distances(source->distances().begin(), source->distances().end());
  std::vector<uint8_t> materials(source->materials().begin(), source->materials().end());
  const SlopeReader reader(volume, coord, distances);
  bool changed = false;

  for (int z = 0; z < terrain::chunkSize; ++z)
  {
    for (int y = 0; y < terrain::chunkSize; ++y)
    {
      for (int x = 0; x < terrain::chunkSize; ++x)
      {
        const auto index = static_cast<size_t>(terrain::sampleIndex(x, y, z));
        if (!isSurfaceSample(distances[index]))
        {
          continue;
        }

        const glm::ivec3 sample = origin + glm::ivec3(x, y, z);
        const uint8_t material = materialFor(sample, reader.normalY(sample), ground);
        changed = changed || material != materials[index];
        materials[index] = material;
      }
    }
  }

  return changed ? terrain::TerrainChunk::dense(distances, std::move(materials)) : nullptr;
}

void paintBands(terrain::TerrainVolume& volume, const Ground& ground)
{
  // Every chunk reads the unedited volume, so a sample's slope never sees a neighbor already repainted.
  terrain::ChunkEdits edits;

  for (const auto& coord : denseChunks(volume))
  {
    if (auto painted = paintChunk(volume, coord, ground))
    {
      edits.emplace_back(coord, std::move(painted));
    }
  }

  terrain::commit(volume, edits);
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
  paintBands(volume, ground);
}

void installDefaultTerrains(terrain::TerrainStore& store)
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

namespace defaultproject {

json buildTerrainShowcase()
{
  return json::array({
    makeObject("Terrain", json::array({
      transform(showcase::terrainOrigin()),
      {
        { "type", "Collider" },
        { "subType", "Terrain" },
        { "terrainUUID", showcaseTerrain },
        { "collisionEnabled", true },
        { "castShadows", true },
        { "renderCollider", false },
        { "isTrigger", false },
        { "layer", 0u },
        { "mask", 0xFFFFFFFFu }
      }
    })),
    player(showcase::playerSpawn(), 0, glm::vec3(0.0f, 0.0f, 1.0f)),
    light({ 0.0f, 300.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, 0.35f, 0.8f, 0.4f)
  });
}

}
