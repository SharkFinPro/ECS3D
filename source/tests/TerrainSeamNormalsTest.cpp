#include <gtest/gtest.h>

#include "DefaultTerrainTilesFixture.h"
#include "TerrainSurfaceFeed.h"
#include "TerrainSurfaceSampling.h"
#include "TerrainVolumeSource.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <VulkanEngine/components/assets/volume/MarchingCubesExtractor.h>
#include <VulkanEngine/components/assets/volume/VolumeChunking.h>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

using basintest::DefaultTerrainTilesTest;
using basintest::tileIndex;

// The end-to-end proof that tiles overlapping by a seam column and an apron on each side shade the seam alike: each
// tile is meshed the way the renderer meshes it (TerrainVolumeSource through the engine's chunk grid and extractor)
// and the vertices on the seam plane are compared.
namespace {
  constexpr int axisX = 0;
  constexpr int axisZ = 2;

  // Vertices on a plane, keyed by their two other coordinates.
  using PlaneVertices = std::map<std::array<float, 2>, vke::SurfaceVertex>;

  vke::VolumeRegion toRegion(const terrain::SampleBox& box)
  {
    return { box.min, box.max };
  }

  // Meshes every chunk of the tile whose cells touch the plane at sample index plane on axis, and collects the vertices
  // lying on it. Open meshes the tile as an open-sided surface; closed pads it with air as a closed one does.
  PlaneVertices meshPlane(const terrain::TerrainVolume& tile, const bool open, const int axis, const int plane)
  {
    const terrain::SampleBox occupied = tile.occupiedBounds();
    const terrain::SampleBox meshed = terrain::desiredSurfaceBounds(occupied, open);
    const terrain::SampleBox readable = open ? terrain::openSampleBounds(meshed) : meshed;

    const auto feed = std::make_shared<const TerrainSurfaceFeed>(
      std::make_shared<const terrain::TerrainVolume::ChunkMap>(tile.snapshotChunks()));
    const TerrainVolumeSource source(feed, meshed, readable);
    const vke::VolumeChunkGrid grid(source.bounds(), source.sampleBounds(), terrain::surfaceChunkCells);
    const vke::MarchingCubesExtractor extractor;

    const float cellSize = tile.voxelSize();
    const float planePosition = static_cast<float>(plane) * cellSize;
    const std::array<int, 2> others = axis == axisX ? std::array<int, 2>{ 1, 2 } : std::array<int, 2>{ 0, 1 };
    PlaneVertices found;

    for (uint32_t chunk = 0; chunk < grid.chunkCount(); ++chunk)
    {
      const vke::CellRange cells = grid.cellRange(chunk);
      if (cells.min[axis] > plane || cells.max[axis] < plane)
      {
        continue;
      }

      const vke::VolumeRegion region = grid.fetchRegion(chunk);
      std::vector<float> distance(vke::sampleCount(region));
      std::vector<uint8_t> material(vke::sampleCount(region));
      source.fill(region, 0, distance, material);

      vke::SurfaceMesh mesh;
      extractor.extract({ region, distance, material }, cells, cellSize, mesh);

      for (const vke::SurfaceVertex& vertex : mesh.vertices)
      {
        if (std::abs(vertex.position[axis] - planePosition) < 1e-3f)
        {
          found.emplace(std::array<float, 2>{ vertex.position[others[0]], vertex.position[others[1]] }, vertex);
        }
      }
    }

    return found;
  }

  float degreesBetween(const glm::vec3& a, const glm::vec3& b)
  {
    return glm::degrees(std::acos(std::clamp(glm::dot(glm::normalize(a), glm::normalize(b)), -1.0f, 1.0f)));
  }

  // The largest lean from vertical among the upward-facing vertices, which leaves out the slab's underside.
  float steepestTilt(const PlaneVertices& vertices)
  {
    float tilt = 0.0f;
    for (const auto& entry : vertices)
    {
      if (entry.second.normal.y <= 0.0f)
      {
        continue;
      }

      tilt = std::max(tilt, degreesBetween(entry.second.normal, { 0.0f, 1.0f, 0.0f }));
    }

    return tilt;
  }

  // How many vertices of a exist in b (the key is the position along the plane, which must match bit for bit), and how
  // many of those have bit-identical normals. Both tiles compute a seam vertex from the same sample values with the same
  // operations, so equality is exact.
  struct Agreement
  {
    int shared = 0;
    int identical = 0;
    float widestNormalGap = 0.0f;
  };

  Agreement compare(const PlaneVertices& a, const PlaneVertices& b)
  {
    Agreement result;
    for (const auto& [key, ours] : a)
    {
      const auto theirs = b.find(key);
      if (theirs == b.end())
      {
        continue;
      }

      ++result.shared;
      result.identical += ours.normal == theirs->second.normal && ours.material == theirs->second.material ? 1 : 0;
      result.widestNormalGap = std::max(result.widestNormalGap, degreesBetween(ours.normal, theirs->second.normal));
    }

    return result;
  }
}

TEST_F(DefaultTerrainTilesTest, OpenSidedTilesShadeAnEastSeamWithIdenticalVertices)
{
  // The south-west pair: its seam climbs the south rim and crosses the moguls, so the normals lean.
  const auto& west = *s_tiles[tileIndex(0, 0)];
  const auto& east = *s_tiles[tileIndex(0, 1)];

  const PlaneVertices ours = meshPlane(west, true, axisX, basin::tileStride + 1);
  const PlaneVertices theirs = meshPlane(east, true, axisX, 1);

  ASSERT_GT(ours.size(), 40u);
  EXPECT_EQ(ours.size(), theirs.size());
  EXPECT_GT(steepestTilt(ours), 15.0f);

  const Agreement agreement = compare(ours, theirs);
  EXPECT_EQ(agreement.shared, static_cast<int>(ours.size()));
  EXPECT_EQ(agreement.identical, agreement.shared);
}

TEST_F(DefaultTerrainTilesTest, OpenSidedTilesShadeANorthSeamWithIdenticalVertices)
{
  const auto& south = *s_tiles[tileIndex(0, 0)];
  const auto& north = *s_tiles[tileIndex(1, 0)];

  const PlaneVertices ours = meshPlane(south, true, axisZ, basin::tileStride + 1);
  const PlaneVertices theirs = meshPlane(north, true, axisZ, 1);

  ASSERT_GT(ours.size(), 40u);
  EXPECT_EQ(ours.size(), theirs.size());
  EXPECT_GT(steepestTilt(ours), 15.0f);

  const Agreement agreement = compare(ours, theirs);
  EXPECT_EQ(agreement.shared, static_cast<int>(ours.size()));
  EXPECT_EQ(agreement.identical, agreement.shared);
}

TEST_F(DefaultTerrainTilesTest, ClosedTilesLightTheSameColumnWithDifferentNormals)
{
  // The canary: a closed surface reads air one sample past its edge column, so that column's normals lean outward,
  // while the neighbor, which holds the same column in its interior, lights it from real samples. The west tile's last
  // column is the east tile's third.
  const auto& west = *s_tiles[tileIndex(0, 0)];
  const auto& east = *s_tiles[tileIndex(0, 1)];

  const PlaneVertices edge = meshPlane(west, false, axisX, basin::tileColumns - 1);
  const PlaneVertices interior = meshPlane(east, false, axisX, basin::tileColumns - 1 - basin::tileStride);

  ASSERT_GT(edge.size(), 40u);
  const Agreement agreement = compare(edge, interior);
  ASSERT_GT(agreement.shared, 40);

  EXPECT_GT(agreement.widestNormalGap, 5.0f);
  EXPECT_LT(agreement.identical, agreement.shared);

  // Positive control: the same column read from the interior of both tiles agrees, so the gap above comes from the
  // closed edge and not from the data.
  const PlaneVertices insideWest = meshPlane(west, false, axisX, basin::tileStride + 1);
  const PlaneVertices insideEast = meshPlane(east, false, axisX, 1);
  const Agreement inside = compare(insideWest, insideEast);
  EXPECT_GT(inside.shared, 40);
  EXPECT_EQ(inside.identical, inside.shared);
}
