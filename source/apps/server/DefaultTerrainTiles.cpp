#include "DefaultTerrainTiles.h"
#include "DefaultProjectParts.h"
#include "DefaultTerrainNoise.h"
#include "DefaultTerrainPaint.h"
#include <Log.h>
#include <terrain/TerrainOps.h>
#include <terrain/TerrainStore.h>
#include <terrain/TerrainTypes.h>
#include <terrain/TerrainVolume.h>
#include <glm/common.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <vector>
#include <uuid.h>

using namespace defaultterrain;

namespace {

constexpr float center = static_cast<float>(basin::worldColumns - 1) * 0.5f;

// How far the rim climbs above the arena floor, in voxels, and over which distances from the center.
constexpr float rimRise = 46.0f;
constexpr float rimStart = 70.0f;
constexpr float rimEnd = 185.0f;

constexpr float terraceStep = 6.0f;
constexpr float hillAmplitude = 6.0f;
constexpr float mogulAmplitude = 3.5f;
constexpr float mogulPeriod = 16.0f;
constexpr float riverDepthMax = 7.0f;
constexpr float riverHalfWidth = 9.0f;

constexpr float lowestGround = 4.0f;
constexpr float grayLine = 60.0f;

// Terraces: the rise quantized into flat ledges joined by short eased risers.
float terraced(const float rise)
{
  const float scaled = rise / terraceStep;
  const float whole = std::floor(scaled);

  return terraceStep * (whole + easeBetween(0.7f, 1.0f, scaled - whole));
}

float northTerraceWeight(const float z)
{
  return easeBetween(210.0f, 250.0f, z);
}

// The rise of the rim above the arena floor. The north is terraced and the east holds a steep bank, which never
// lowers the rim the plain slope already built.
float rimHeight(const float x, const float z)
{
  const float r = distance2D(x, z, center, center);
  const float slope = rimRise * easeBetween(rimStart, rimEnd, r);
  const float northWeight = northTerraceWeight(z);
  const float bankWeight = easeBetween(240.0f, 262.0f, x) * (1.0f - northWeight);
  const float bank = rimRise * easeBetween(290.0f, 305.0f, x - (z - center) * (z - center) / 300.0f);
  const float eastern = glm::mix(slope, std::max(slope, bank), bankWeight);

  return glm::mix(eastern, terraced(slope), northWeight);
}

// Bumps on the south-west corner, fading out toward the middle of the basin.
float moguls(const float x, const float z)
{
  const float turn = 2.0f * std::numbers::pi_v<float> / mogulPeriod;
  const float weight = (1.0f - easeBetween(104.0f, 136.0f, x)) * (1.0f - easeBetween(104.0f, 136.0f, z));

  return mogulAmplitude * std::sin(x * turn) * std::sin(z * turn) * weight;
}

// How deep the dry riverbed cuts at a column, in voxels: a channel that winds in from the west edge and fades out
// before the arena.
float riverCut(const float x, const float z)
{
  const float centerline = center + 18.0f * std::sin(x * 0.05f + 0.7f);
  const float profile = 1.0f - easeBetween(2.0f, riverHalfWidth, std::abs(z - centerline));
  const float reach = 1.0f - easeBetween(145.0f, 160.0f, x);
  const float arenaFade = easeBetween(46.0f, 62.0f, distance2D(x, z, center, center));

  return riverDepthMax * profile * reach * arenaFade;
}

// The global heightfield sampled once, with the slope terms the heightmap fill derives from it.
class GlobalGround
{
public:
  GlobalGround()
    : m_heights(static_cast<size_t>(basin::worldColumns) * static_cast<size_t>(basin::worldColumns))
  {
    for (int z = 0; z < basin::worldColumns; ++z)
    {
      for (int x = 0; x < basin::worldColumns; ++x)
      {
        m_heights[index(x, z)] = basin::groundAt(static_cast<float>(x), static_cast<float>(z));
      }
    }
  }

  [[nodiscard]] float at(const int x, const int z) const
  {
    return m_heights[index(std::clamp(x, 0, basin::worldColumns - 1), std::clamp(z, 0, basin::worldColumns - 1))];
  }

  // The height as the fill stores it: a 16-bit fraction of the full scale.
  [[nodiscard]] float quantized(const int x, const int z) const
  {
    constexpr float full = static_cast<float>(basin::maxHeight);

    return static_cast<float>(std::lround(at(x, z) / full * 65535.0f)) / 65535.0f * full;
  }

  // The distance to the surface in voxels at global column (x, z) and height y, from the fill's first order estimate
  // over central differences. It reads past the tile edge, which is what lets two tiles agree on their shared column.
  [[nodiscard]] int16_t distanceAt(const int x, const int y, const int z) const
  {
    const float gradX = (quantized(x + 1, z) - quantized(x - 1, z)) * 0.5f;
    const float gradZ = (quantized(x, z + 1) - quantized(x, z - 1)) * 0.5f;
    const float inverseNorm = 1.0f / std::sqrt(1.0f + gradX * gradX + gradZ * gradZ);

    return terrain::encodeDistance((static_cast<float>(y) - quantized(x, z)) * inverseNorm);
  }

  [[nodiscard]] float normalY(const int x, const int z) const
  {
    const float gradX = (at(x + 1, z) - at(x - 1, z)) * 0.5f;
    const float gradZ = (at(x, z + 1) - at(x, z - 1)) * 0.5f;

    return 1.0f / std::sqrt(1.0f + gradX * gradX + gradZ * gradZ);
  }

private:
  [[nodiscard]] static size_t index(const int x, const int z)
  {
    return static_cast<size_t>(z) * static_cast<size_t>(basin::worldColumns) + static_cast<size_t>(x);
  }

  std::vector<float> m_heights;
};

std::vector<float> tileHeights(const GlobalGround& ground, const int row, const int col)
{
  std::vector<float> heights(static_cast<size_t>(basin::tileColumns) * static_cast<size_t>(basin::tileColumns));
  const int offsetX = col * basin::tileStride;
  const int offsetZ = row * basin::tileStride;

  for (int z = 0; z < basin::tileColumns; ++z)
  {
    for (int x = 0; x < basin::tileColumns; ++x)
    {
      const auto index = static_cast<size_t>(z) * basin::tileColumns + static_cast<size_t>(x);
      heights[index] = ground.at(offsetX + x, offsetZ + z);
    }
  }

  return heights;
}

// The heightmap fill clamps its slope to the tile at the outermost columns, so a shared edge would differ between the
// two tiles that hold it. Rewriting those columns from the global heights makes both agree exactly.
void matchEdgeColumns(terrain::TerrainVolume& volume, const GlobalGround& ground, const int offsetX, const int offsetZ)
{
  const terrain::SampleBox bounds = volume.occupiedBounds();
  const int last = basin::tileColumns - 1;
  const terrain::SampleBox strips[] = {
    { { 0, bounds.min.y, 0 }, { 1, bounds.max.y, basin::tileColumns } },
    { { last, bounds.min.y, 0 }, { basin::tileColumns, bounds.max.y, basin::tileColumns } },
    { { 0, bounds.min.y, 0 }, { basin::tileColumns, bounds.max.y, 1 } },
    { { 0, bounds.min.y, last }, { basin::tileColumns, bounds.max.y, basin::tileColumns } }
  };

  for (const auto& strip : strips)
  {
    const auto edit = [&](const glm::ivec3 sample, int16_t& distance, uint8_t&)
    {
      distance = ground.distanceAt(offsetX + sample.x, sample.y, offsetZ + sample.z);
    };

    terrain::commit(volume, terrain::editSamples(volume, strip, edit));
  }
}

// Sand on the arena floor and in the riverbed, grass on gentle slopes, brown on steep faces and under the surface,
// gray on the high ground. The slope comes from the global heights so a shared edge paints the same on both tiles.
uint8_t surfaceMaterial(const GlobalGround& ground, const int x, const int y, const int z)
{
  const float height = ground.at(x, z);

  if (height - static_cast<float>(y) > 3.0f || ground.normalY(x, z) < 0.6f)
  {
    return materialBrown;
  }

  const bool onFloor = height < static_cast<float>(basin::groundHeight) + 1.0f;
  if (onFloor || riverCut(static_cast<float>(x), static_cast<float>(z)) > 2.0f)
  {
    return materialSand;
  }

  return height > grayLine ? materialGray : materialGrass;
}

void generateTile(terrain::TerrainVolume& volume, const GlobalGround& ground, const int row, const int col)
{
  const int offsetX = col * basin::tileStride;
  const int offsetZ = row * basin::tileStride;

  fillHeights(volume, tileHeights(ground, row, col), basin::tileColumns, static_cast<float>(basin::maxHeight),
              groundThickness, interiorMaterial);
  matchEdgeColumns(volume, ground, offsetX, offsetZ);
  paintSurface(volume, basin::tileColumns, [&](const glm::ivec3 sample, float)
  {
    return surfaceMaterial(ground, offsetX + sample.x, sample.y, offsetZ + sample.z);
  });
}

}

namespace basin
{
  float groundAt(const float x, const float z)
  {
    const float r = distance2D(x, z, center, center);
    const float northWeight = northTerraceWeight(z);
    const float hills = hillAmplitude * easeBetween(rimStart, rimEnd, r) * fbm(x + 1013.5f, z - 733.25f) *
                        (1.0f - 0.9f * northWeight);
    const float height = static_cast<float>(groundHeight) + rimHeight(x, z) + hills + moguls(x, z) - riverCut(x, z);

    return std::clamp(height, lowestGround, static_cast<float>(maxHeight) - 1.0f);
  }

  glm::vec3 tileOrigin(const int row, const int col)
  {
    const float half = static_cast<float>(worldColumns - 1) * voxelSize * 0.5f;

    return { static_cast<float>(col * tileStride) * voxelSize - half, -static_cast<float>(groundHeight) * voxelSize,
             static_cast<float>(row * tileStride) * voxelSize - half };
  }

  float worldGroundY(const float worldX, const float worldZ)
  {
    const glm::vec3 origin = tileOrigin(0, 0);

    return (groundAt((worldX - origin.x) / voxelSize, (worldZ - origin.z) / voxelSize) -
            static_cast<float>(groundHeight)) * voxelSize;
  }
}

void generateBasinTiles(const std::array<terrain::TerrainVolume*, basin::tileCount>& tiles)
{
  const GlobalGround ground;

  for (int row = 0; row < basin::tilesPerSide; ++row)
  {
    for (int col = 0; col < basin::tilesPerSide; ++col)
    {
      if (auto* volume = tiles[static_cast<size_t>(row * basin::tilesPerSide + col)])
      {
        generateTile(*volume, ground, row, col);
      }
    }
  }
}

void installBasinTerrains(terrain::TerrainStore& store)
{
  std::array<std::shared_ptr<terrain::TerrainVolume>, basin::tileCount> volumes;
  std::array<terrain::TerrainVolume*, basin::tileCount> tiles{};

  for (size_t index = 0; index < volumes.size(); ++index)
  {
    const auto uuid = uuids::uuid::from_string(defaultproject::basinTerrains[index]);
    const auto* current = uuid ? store.description(*uuid) : nullptr;
    if (!current)
    {
      continue;
    }

    auto description = *current;
    description.voxelSize = basin::voxelSize;
    if (!store.applyDescription(description))
    {
      Log::warn(LogCategory::server, "A Terrain Basin tile's voxel size was refused; it was not generated");
      continue;
    }

    volumes[index] = store.find(*uuid);
    tiles[index] = volumes[index].get();
  }

  generateBasinTiles(tiles);

  for (size_t index = 0; index < volumes.size(); ++index)
  {
    if (!volumes[index])
    {
      continue;
    }

    const auto uuid = uuids::uuid::from_string(defaultproject::basinTerrains[index]).value();
    auto description = *store.description(uuid);
    description.chunkCount = static_cast<uint32_t>(volumes[index]->chunks().size());
    store.applyDescription(description);
    store.stampAllChunks(uuid);
  }
}
