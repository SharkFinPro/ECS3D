#include "DefaultTerrainCaves.h"
#include "DefaultTerrainNoise.h"
#include "DefaultTerrainPaint.h"
#include "DefaultTerrainTiles.h"
#include <terrain/TerrainOps.h>
#include <terrain/TerrainTypes.h>
#include <terrain/TerrainVolume.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <array>
#include <cstdint>

using terrain::SampleBox;

namespace {

// The arena floor, where every chamber and passage floor sits.
constexpr float floorLevel = static_cast<float>(basin::groundHeight);

// The rock face's roughness, in voxels.
constexpr float roughness = 1.2f;

float ellipsoid(const glm::vec3& point, const glm::vec3& center, const glm::vec3& radii)
{
  const float smallest = std::min({ radii.x, radii.y, radii.z });

  return (glm::length((point - center) / radii) - 1.0f) * smallest;
}

float capsule(const glm::vec3& point, const glm::vec3& from, const glm::vec3& to, const float radius)
{
  const glm::vec3 along = to - from;
  const float t = std::clamp(glm::dot(point - from, along) / glm::dot(along, along), 0.0f, 1.0f);

  return glm::length(point - from - along * t) - radius;
}

float roundBox(const glm::vec3& point, const glm::vec3& center, const glm::vec3& half, const float rounding)
{
  const glm::vec3 q = glm::abs(point - center) - half + rounding;

  return glm::length(glm::max(q, glm::vec3(0.0f))) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f) - rounding;
}

// A passage of elliptical section that runs along x, centered on (z, y).
float passageAlongX(const glm::vec3& point, const float centerZ, const float centerY, const float radiusZ,
                    const float radiusY)
{
  const glm::vec2 scaled((point.z - centerZ) / radiusZ, (point.y - centerY) / radiusY);

  return (glm::length(scaled) - 1.0f) * std::min(radiusZ, radiusY);
}

float smoothUnion(const float a, const float b, const float blend)
{
  const float h = std::max(blend - std::abs(a - b), 0.0f) / blend;

  return std::min(a, b) - h * h * blend * 0.25f;
}

float rough(const glm::vec3& point)
{
  return roughness * defaultterrain::valueNoise(point.x / 9.0f + point.y * 0.31f, point.z / 9.0f - point.y * 0.47f, 21);
}

// The ground with a cavity subtracted, kept flat at the arena floor unless the cavity's floor is raised.
float cavityInGround(const float ground, const float cavity, const glm::vec3& point, const float slope = 0.0f,
                     const float slopeStart = 0.0f)
{
  const float rise = slope * std::max(point.x - slopeStart, 0.0f);

  return std::min(std::max(ground, -cavity), (point.y - floorLevel - rise) / std::sqrt(1.0f + slope * slope));
}

// A tunnel bored from the arena floor into the terraced north rim. Its roof is open to the sky while the slope is low,
// then closes into a domed chamber.
const glm::vec3 northChamber{ 188.5f, floorLevel, 332.0f };

float northCave(const glm::vec3& point, const float ground)
{
  const float chamber = ellipsoid(point, northChamber, { 22.0f, 15.0f, 24.0f });
  const float tunnel = capsule(point, { 188.5f, 29.0f, 262.0f }, { 188.5f, 29.0f, 326.0f }, 9.0f);

  return cavityInGround(ground, smoothUnion(chamber, tunnel, 5.0f) + rough(point), point);
}

// A cavern in the east bank, entered by a tunnel through the cliff. A shaft from the plateau drops onto a ramp that
// runs down to the cavern's level floor, so whatever falls in rolls out through the tunnel.
constexpr float eastRampSlope = 0.5f;
constexpr float eastRampStart = 338.0f;
const glm::vec3 eastChamber{ 330.0f, floorLevel, 188.5f };

float eastCavern(const glm::vec3& point, const float ground)
{
  const float chamber = ellipsoid(point, eastChamber, { 24.0f, 15.0f, 24.0f });
  const float tunnel = capsule(point, { 276.0f, 29.0f, 188.5f }, { 324.0f, 29.0f, 188.5f }, 9.0f);
  const float shaft = capsule(point, { 346.0f, 26.0f, 200.0f }, { 346.0f, 90.0f, 200.0f }, 6.0f);
  const float cavity = smoothUnion(smoothUnion(chamber, tunnel, 5.0f), shaft, 4.0f);

  return cavityInGround(ground, cavity + rough(point), point, eastRampSlope, eastRampStart);
}

// A slab of rock across the arena floor with an arch cut through it along x. The floor runs through the passage.
const glm::vec3 archCenter{ 156.0f, 23.0f, 188.5f };
constexpr float archTop = 46.0f;

float rockArch(const glm::vec3& point, const float ground)
{
  const float slab = roundBox(point, archCenter, { 13.0f, 23.0f, 26.0f }, 4.0f) + 0.5f * rough(point);
  const float passage = passageAlongX(point, archCenter.z, floorLevel, 16.0f, 16.0f);

  return std::min(ground, std::max(slab, -passage));
}

struct Feature
{
  int row;
  int col;
  SampleBox box;
  float (*distance)(const glm::vec3& point, float ground);
};

const std::array<Feature, basin::caves::featureCount> features = { {
  { 2, 1, { { 156, 0, 258 }, { 221, 72, 358 } }, northCave },
  { 1, 2, { { 262, 0, 150 }, { 362, 84, 231 } }, eastCavern },
  { 1, 1, { { 136, 0, 154 }, { 176, 64, 224 } }, rockArch }
} };

float worldX(const float column)
{
  return column * basin::voxelSize + basin::tileOrigin(0, 0).x;
}

float worldZ(const float column)
{
  return column * basin::voxelSize + basin::tileOrigin(0, 0).z;
}

float worldY(const float sampleY)
{
  return (sampleY - floorLevel) * basin::voxelSize;
}

}

namespace basin::caves
{
  void carveTile(terrain::TerrainVolume& volume, const int row, const int col, const int offsetX, const int offsetZ,
                 const GroundDistance& ground)
  {
    const glm::ivec3 offset(offsetX, 0, offsetZ);

    for (const auto& feature : features)
    {
      if (feature.row != row || feature.col != col)
      {
        continue;
      }

      const SampleBox local{ feature.box.min - offset, feature.box.max - offset };
      const auto edit = [&](const glm::ivec3 sample, int16_t& distance, uint8_t& material)
      {
        const glm::ivec3 global = sample + offset;
        const float plain = ground(global.x, global.y, global.z);

        distance = terrain::encodeDistance(feature.distance(glm::vec3(global), plain));
        material = defaultterrain::interiorMaterial;
      };

      terrain::commit(volume, terrain::editSamples(volume, local, edit));
    }
  }

  bool isArchRock(const int x, const int y, const int z, const float normalY)
  {
    constexpr int topSurface = static_cast<int>(floorLevel) + 6;

    return x >= 140 && x <= 172 && z >= 155 && z <= 222 && y > static_cast<int>(floorLevel) &&
           (normalY < 0.8f || y >= topSurface);
  }

  bool isFeatureColumn(const int x, const int z)
  {
    return std::ranges::any_of(features, [x, z](const Feature& feature)
    {
      return x >= feature.box.min.x && x < feature.box.max.x && z >= feature.box.min.z && z < feature.box.max.z;
    });
  }

  const std::array<Stack, stackCount>& stacks()
  {
    static const std::array<Stack, stackCount> list = { {
      { "North cave", worldX(northChamber.x), worldZ(northChamber.z), worldY(floorLevel), true, 3, 3 },
      { "East cavern", worldX(eastChamber.x), worldZ(eastChamber.z), worldY(floorLevel), true, 3, 3 },
      { "Arch top", worldX(archCenter.x), worldZ(archCenter.z), worldY(archTop), false, 3, 2 },
      { "Arch passage", worldX(archCenter.x), worldZ(archCenter.z), worldY(floorLevel), true, 3, 2 }
    } };

    return list;
  }

  float stackHalfSpan(const Stack& stack)
  {
    // A stack's slots are stackSpacing apart and its bodies stay within their slot.
    return static_cast<float>(stack.columns) * stackSpacing * 0.5f;
  }
}
