#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "DefaultProjectParts.h"
#include "DefaultShapes.h"
#include "DefaultTerrain.h"
#include "DefaultTerrainTiles.h"
#include "PhysicsSystem.h"
#include "TestScene.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/collisions/ConvexMeshCollider.h"
#include "objects/components/collisions/Terrain.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainRaycast.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <uuid.h>

namespace {
  using Tiles = std::array<std::shared_ptr<terrain::TerrainVolume>, basin::tileCount>;

  constexpr float voxel = basin::voxelSize;
  constexpr int last = basin::tileColumns - 1;

  AssetRecord terrainRecord(const std::string& uuid)
  {
    const auto parsed = uuids::uuid::from_string(uuid).value();

    return { .uuid = parsed, .type = AssetType::Terrain, .path = terrainAssetPath(parsed) };
  }

  size_t tileIndex(const int row, const int col)
  {
    return static_cast<size_t>(row * basin::tilesPerSide + col);
  }

  bool sameChunks(const terrain::TerrainVolume& a, const terrain::TerrainVolume& b)
  {
    if (a.chunks().size() != b.chunks().size())
    {
      return false;
    }

    for (const auto& [coord, chunk] : a.chunks())
    {
      const auto other = b.chunk(coord);
      if (!other || other->hash() != chunk->hash())
      {
        return false;
      }
    }

    return true;
  }

  // The materials of the solid samples inside the distance band, which is where the surface reads them.
  void addSurfaceMaterials(const terrain::TerrainVolume& volume, std::set<uint8_t>& materials)
  {
    for (const auto& [coord, chunk] : volume.chunks())
    {
      if (chunk->isUniform())
      {
        continue;
      }

      for (int index = 0; index < terrain::samplesPerChunk; ++index)
      {
        const int16_t distance = chunk->distanceAtIndex(index);
        if (distance < 0 && distance > -terrain::airDistance)
        {
          materials.insert(chunk->materialAtIndex(index));
        }
      }
    }
  }

  // The world height of the ground a ray straight down from above finds at a tile-local column, or nothing.
  std::optional<float> hitHeight(const terrain::TerrainVolume& volume, const int row, const int col, const float x,
                                 const float z)
  {
    const glm::vec3 origin(x * voxel, static_cast<float>(basin::maxHeight) * voxel + 10.0f, z * voxel);
    const auto hit = terrain::raycast(volume, origin, { 0.0f, -1.0f, 0.0f }, 1000.0f);

    return hit ? std::optional<float>(hit->point.y + basin::tileOrigin(row, col).y) : std::nullopt;
  }

  // The tallest y a sample on a tile's edge can have in either tile; every sample above it is air in both.
  int topSample(const Tiles& tiles, const size_t a, const size_t b)
  {
    return std::max(tiles[a]->occupiedBounds().max.y, tiles[b]->occupiedBounds().max.y);
  }

  class DefaultTerrainTilesTest : public testing::Test
  {
  protected:
    static void SetUpTestSuite()
    {
      std::array<terrain::TerrainVolume*, basin::tileCount> pointers{};
      for (size_t index = 0; index < s_tiles.size(); ++index)
      {
        s_tiles[index] = std::make_shared<terrain::TerrainVolume>(basin::voxelSize);
        pointers[index] = s_tiles[index].get();
      }

      const auto start = std::chrono::steady_clock::now();
      generateBasinTiles(pointers);
      s_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }

    static void TearDownTestSuite()
    {
      for (auto& tile : s_tiles)
      {
        tile.reset();
      }
    }

    static size_t totalChunks()
    {
      size_t total = 0;
      for (const auto& tile : s_tiles)
      {
        total += tile->chunks().size();
      }

      return total;
    }

    static Tiles s_tiles;
    static double s_seconds;
  };

  Tiles DefaultTerrainTilesTest::s_tiles;
  double DefaultTerrainTilesTest::s_seconds = 0.0;
}

TEST_F(DefaultTerrainTilesTest, NeighborsShareTheirEdgeSamplesExactly)
{
  for (int row = 0; row < basin::tilesPerSide; ++row)
  {
    for (int col = 0; col < basin::tilesPerSide; ++col)
    {
      for (const bool east : { true, false })
      {
        const int neighborRow = east ? row : row + 1;
        const int neighborCol = east ? col + 1 : col;
        if (neighborRow >= basin::tilesPerSide || neighborCol >= basin::tilesPerSide)
        {
          continue;
        }

        const size_t a = tileIndex(row, col);
        const size_t b = tileIndex(neighborRow, neighborCol);
        SCOPED_TRACE("tile " + std::to_string(a) + (east ? " east of " : " south of ") + std::to_string(b));

        int mismatches = 0;
        for (int along = 0; along <= last; ++along)
        {
          for (int y = 0; y < topSample(s_tiles, a, b); ++y)
          {
            const int16_t ours = east ? s_tiles[a]->distanceAtSample({ last, y, along })
                                      : s_tiles[a]->distanceAtSample({ along, y, last });
            const int16_t theirs = east ? s_tiles[b]->distanceAtSample({ 0, y, along })
                                        : s_tiles[b]->distanceAtSample({ along, y, 0 });
            mismatches += ours != theirs ? 1 : 0;
          }
        }

        EXPECT_EQ(mismatches, 0);
      }
    }
  }
}

TEST_F(DefaultTerrainTilesTest, TheColumnInsideAnEdgeDiffersFromTheEdge)
{
  // The positive control for the seam check: it can tell two different columns apart.
  const auto& tile = *s_tiles[tileIndex(1, 1)];
  const int top = tile.occupiedBounds().max.y;

  int differing = 0;
  for (int z = 0; z <= last; ++z)
  {
    for (int y = 0; y < top; ++y)
    {
      differing += tile.distanceAtSample({ last, y, z }) != tile.distanceAtSample({ last - 1, y, z }) ? 1 : 0;
    }
  }

  EXPECT_GT(differing, 100);
}

TEST_F(DefaultTerrainTilesTest, ARayStraightDownOnASeamHitsTheSameHeightFromBothTiles)
{
  const float columns[] = { 8.0f, 37.5f, 64.0f, 99.0f, 120.25f };
  int compared = 0;
  float widestStep = 0.0f;

  for (int row = 0; row < basin::tilesPerSide; ++row)
  {
    for (int col = 0; col < basin::tilesPerSide; ++col)
    {
      for (const bool east : { true, false })
      {
        const int neighborRow = east ? row : row + 1;
        const int neighborCol = east ? col + 1 : col;
        if (neighborRow >= basin::tilesPerSide || neighborCol >= basin::tilesPerSide)
        {
          continue;
        }

        for (const float along : columns)
        {
          const auto ours = hitHeight(*s_tiles[tileIndex(row, col)], row, col, east ? static_cast<float>(last) : along,
                                      east ? along : static_cast<float>(last));
          const auto theirs = hitHeight(*s_tiles[tileIndex(neighborRow, neighborCol)], neighborRow, neighborCol,
                                        east ? 0.0f : along, east ? along : 0.0f);
          ASSERT_TRUE(ours.has_value());
          ASSERT_TRUE(theirs.has_value());
          EXPECT_NEAR(*ours, *theirs, 0.01f) << row << "," << col << (east ? " east " : " south ") << along;
          ++compared;

          // One voxel in from the edge the ground is somewhere else.
          const auto inside = hitHeight(*s_tiles[tileIndex(row, col)], row, col,
                                        east ? static_cast<float>(last) - 1.0f : along,
                                        east ? along : static_cast<float>(last) - 1.0f);
          ASSERT_TRUE(inside.has_value());
          widestStep = std::max(widestStep, std::abs(*inside - *ours));
        }
      }
    }
  }

  EXPECT_EQ(compared, 12 * 5);
  EXPECT_GT(widestStep, 0.1f);
}

TEST_F(DefaultTerrainTilesTest, TheChunkCountStaysWithinWhatEveryJoinStreams)
{
  EXPECT_LE(totalChunks(), 600u);
  EXPECT_GE(totalChunks(), static_cast<size_t>(basin::tileCount) * 16u);
}

TEST_F(DefaultTerrainTilesTest, GenerationStaysQuick)
{
#ifndef NDEBUG
  GTEST_SKIP() << "A Release measurement: Debug, sanitizer and coverage builds skip it";
#endif
  std::cout << "[ INFO ] basin generation took " << s_seconds << " s for " << totalChunks() << " chunks\n";
  EXPECT_LT(s_seconds, 3.0);
}

namespace {
  float height(const int x, const int z)
  {
    return basin::groundAt(static_cast<float>(x), static_cast<float>(z));
  }

  // How many flat runs of at least four columns stand above the arena floor, each at its own height, along a line.
  int plateausAlong(const std::function<float(int)>& heightAt, const int from, const int to)
  {
    std::vector<float> plateaus;
    int run = 0;
    float start = 0.0f;

    const auto close = [&]()
    {
      const bool high = start > static_cast<float>(basin::groundHeight) + 3.0f;
      const bool fresh = std::ranges::none_of(plateaus, [start](const float other)
      {
        return std::abs(other - start) < 3.0f;
      });
      if (run >= 4 && high && fresh)
      {
        plateaus.push_back(start);
      }

      run = 0;
    };

    for (int i = from; i < to; ++i)
    {
      if (std::abs(heightAt(i + 1) - heightAt(i)) < 0.05f)
      {
        start = run == 0 ? heightAt(i) : start;
        ++run;
      }
      else
      {
        close();
      }
    }

    close();

    return static_cast<int>(plateaus.size());
  }

  // The largest second difference at the mogul period's half step over a 40-column patch: zero on a straight slope,
  // large on a field of bumps.
  float bumpiness(const int x0, const int z0)
  {
    float largest = 0.0f;
    for (int z = z0; z < z0 + 40; ++z)
    {
      for (int x = x0; x < x0 + 40; ++x)
      {
        const float alongX = height(x + 8, z) + height(x - 8, z) - 2.0f * height(x, z);
        const float alongZ = height(x, z + 8) + height(x, z - 8) - 2.0f * height(x, z);
        largest = std::max({ largest, std::abs(alongX), std::abs(alongZ) });
      }
    }

    return largest;
  }

  float steepestStep(const int x0, const int x1, const int z0, const int z1)
  {
    float steepest = 0.0f;
    for (int z = z0; z < z1; ++z)
    {
      for (int x = x0; x < x1; ++x)
      {
        steepest = std::max(steepest, std::abs(height(x + 1, z) - height(x, z)));
      }
    }

    return steepest;
  }
}

TEST_F(DefaultTerrainTilesTest, TheArenaIsFlatAndTheRimRisesFarAboveIt)
{
  const float centerColumn = static_cast<float>(basin::worldColumns - 1) / 2.0f;
  float lowest = 1e9f;
  float highest = -1e9f;

  for (int z = 0; z < basin::worldColumns; ++z)
  {
    for (int x = 0; x < basin::worldColumns; ++x)
    {
      if (std::hypot(static_cast<float>(x) - centerColumn, static_cast<float>(z) - centerColumn) < 40.0f)
      {
        lowest = std::min(lowest, height(x, z));
        highest = std::max(highest, height(x, z));
      }
    }
  }

  EXPECT_LT(highest - lowest, 1.0f);
  EXPECT_NEAR(lowest, static_cast<float>(basin::groundHeight), 1.0f);

  const int edge = basin::worldColumns - 1;
  for (const auto& [x, z] : { std::pair{ 0, 0 }, std::pair{ edge, 0 }, std::pair{ 0, edge }, std::pair{ edge, edge } })
  {
    EXPECT_GE(height(x, z), static_cast<float>(basin::groundHeight) + 30.0f) << x << "," << z;
  }
}

TEST_F(DefaultTerrainTilesTest, TheVolumeHoldsTheGroundTheFunctionDescribes)
{
  // The raycast lands within a voxel of groundAt: on the arena, on a terrace, on the bank and in the riverbed.
  const std::pair<int, int> columns[] = { { 190, 190 }, { 190, 300 }, { 300, 190 }, { 60, 200 }, { 60, 60 } };

  for (const auto& [x, z] : columns)
  {
    const int col = std::min(x / basin::tileStride, basin::tilesPerSide - 1);
    const int row = std::min(z / basin::tileStride, basin::tilesPerSide - 1);
    const auto hit = hitHeight(*s_tiles[tileIndex(row, col)], row, col, static_cast<float>(x - col * basin::tileStride),
                               static_cast<float>(z - row * basin::tileStride));
    ASSERT_TRUE(hit.has_value()) << x << "," << z;

    const float expected = (height(x, z) - static_cast<float>(basin::groundHeight)) * voxel;
    EXPECT_NEAR(*hit, expected, voxel) << x << "," << z;
  }
}

TEST_F(DefaultTerrainTilesTest, TheNorthStepsIntoTerracesAndTheOtherSidesDoNot)
{
  const auto line = [](const int x) { return [x](const int z) { return height(x, z); }; };

  // Row 2 across all three tiles.
  for (const int x : { 60, 190, 320 })
  {
    EXPECT_GE(plateausAlong(line(x), 254, 380), 3) << x;
  }

  // Positive control: the same rim to the south climbs in one smooth slope.
  EXPECT_EQ(plateausAlong(line(190), 10, 120), 0);
}

TEST_F(DefaultTerrainTilesTest, TheRiverbedCutsBelowBothBanksAcrossTheWestSeam)
{
  const int seam = basin::tileStride;
  int bedZ = 150;
  for (int z = 150; z < 230; ++z)
  {
    bedZ = height(seam, z) < height(seam, bedZ) ? z : bedZ;
  }

  const float bed = height(seam, bedZ);
  EXPECT_LE(bed + 5.0f, std::min(height(seam, bedZ - 14), height(seam, bedZ + 14)));

  // Positive control: on the same seam to the south the ground is a plain slope, with no channel in it.
  const float control = height(seam, 60);
  EXPECT_GT(control + 5.0f, std::min(height(seam, 60 - 14), height(seam, 60 + 14)));
}

TEST_F(DefaultTerrainTilesTest, TheEastBankIsSteepAndTheSouthRimIsNot)
{
  EXPECT_GT(steepestStep(254, 380, 0, 254), 1.5f);

  // Positive control: the south-center tile's rim is a gentle slope.
  EXPECT_LT(steepestStep(127, 253, 0, 126), 1.0f);
}

TEST_F(DefaultTerrainTilesTest, MogulsBumpTheSouthWestAndNowhereElse)
{
  // Peak to trough, a mogul field swings the ground by twice its amplitude, so the second difference reaches four
  // times it.
  EXPECT_GE(bumpiness(20, 20), 8.0f);

  // Positive control: the south-east tile has the same hills and slope without the bumps.
  EXPECT_LT(bumpiness(260, 12), 4.0f);
}

TEST_F(DefaultTerrainTilesTest, TheSurfaceUsesSeveralMaterialsAndNeverTheReservedOne)
{
  std::set<uint8_t> materials;
  for (const auto& tile : s_tiles)
  {
    addSurfaceMaterials(*tile, materials);
  }

  EXPECT_TRUE(materials.contains(0));
  EXPECT_TRUE(materials.contains(1));
  EXPECT_TRUE(materials.contains(2));
  EXPECT_TRUE(materials.contains(4));
  EXPECT_FALSE(materials.contains(terrain::reservedMaterial));
}

TEST_F(DefaultTerrainTilesTest, InstallFillsEveryTileAndTheShowcaseDeterministically)
{
  AssetRegistry registry;
  registry.registerAsset(terrainRecord(defaultproject::showcaseTerrain));
  for (const auto& uuid : defaultproject::basinTerrains)
  {
    registry.registerAsset(terrainRecord(uuid));
  }

  terrain::TerrainStore store;
  store.reconcile(registry);
  installDefaultTerrains(store);

  for (size_t index = 0; index < defaultproject::basinTerrains.size(); ++index)
  {
    SCOPED_TRACE(index);
    const auto uuid = uuids::uuid::from_string(defaultproject::basinTerrains[index]).value();
    const auto volume = store.find(uuid);
    ASSERT_NE(volume, nullptr);
    ASSERT_FALSE(volume->chunks().empty());

    EXPECT_TRUE(sameChunks(*volume, *s_tiles[index]));
    EXPECT_FLOAT_EQ(volume->voxelSize(), basin::voxelSize);
    EXPECT_FLOAT_EQ(store.description(uuid)->voxelSize, basin::voxelSize);
    EXPECT_EQ(store.description(uuid)->chunkCount, volume->chunks().size());

    for (const auto& [coord, chunk] : volume->chunks())
    {
      EXPECT_GT(store.revision(uuid, coord), 0u);
    }
  }

  // Positive control: the comparison tells two different tiles apart.
  EXPECT_FALSE(sameChunks(*s_tiles[0], *s_tiles[4]));

  const auto showcase = store.find(uuids::uuid::from_string(defaultproject::showcaseTerrain).value());
  ASSERT_NE(showcase, nullptr);
  EXPECT_FALSE(showcase->chunks().empty());
  EXPECT_FALSE(terrain::firstIncompleteTerrain(store).has_value());
}

TEST_F(DefaultTerrainTilesTest, InstallGeneratesTheTilesItHoldsAndLeavesOtherTerrainsEmpty)
{
  const auto otherUuid = "11111111-1111-1111-1111-111111111111";

  AssetRegistry registry;
  registry.registerAsset(terrainRecord(defaultproject::basinTerrains[0]));
  registry.registerAsset(terrainRecord(defaultproject::basinTerrains[4]));
  registry.registerAsset(terrainRecord(otherUuid));

  terrain::TerrainStore store;
  store.reconcile(registry);
  installDefaultTerrains(store);

  for (const size_t index : { size_t{ 0 }, size_t{ 4 } })
  {
    const auto volume = store.find(uuids::uuid::from_string(defaultproject::basinTerrains[index]).value());
    ASSERT_NE(volume, nullptr);
    EXPECT_TRUE(sameChunks(*volume, *s_tiles[index]));
  }

  EXPECT_EQ(store.find(uuids::uuid::from_string(defaultproject::basinTerrains[1]).value()), nullptr);

  const auto other = store.find(uuids::uuid::from_string(otherUuid).value());
  ASSERT_NE(other, nullptr);
  EXPECT_TRUE(other->chunks().empty());
  EXPECT_EQ(store.description(uuids::uuid::from_string(otherUuid).value())->chunkCount, 0u);
}

namespace {
  struct Bodies
  {
    std::shared_ptr<Object> sphere;
    std::shared_ptr<Object> d20;
    std::shared_ptr<Object> capsule;
  };

  struct SeamDrop
  {
    float lowestGap = 1e9f;
    float lowestCenter = 1e9f;
    float lastSecondGap = 0.0f;
    float finalGap = 0.0f;
    float drift = 0.0f;
  };

  float lowestWorldY(ConvexMeshCollider& collider)
  {
    float lowest = std::numeric_limits<float>::max();
    for (const auto& vertex : collider.getWorldVertices())
    {
      lowest = std::min(lowest, vertex.y);
    }

    return lowest;
  }

  // The body's lowest point over the basin's ground under its center.
  float gapBelow(const std::shared_ptr<Object>& object, const std::string& kind)
  {
    const glm::vec3 position = fixtures::positionOf(object);
    float bottom = position.y - 1.0f;
    if (kind == "capsule")
    {
      bottom = position.y - 2.0f;
    }
    else if (kind == "d20")
    {
      bottom = lowestWorldY(*std::dynamic_pointer_cast<ConvexMeshCollider>(
        object->getComponent<Collider>(ComponentType::collider)));
    }

    return bottom - basin::worldGroundY(position.x, position.z);
  }

  // A sphere, a D20 and an upright capsule dropped three units above the ground over a seam between two tiles at
  // world (seamX, seamZ), a few units apart along z, for three seconds of the server's ticks.
  std::array<SeamDrop, 3> dropAcrossSeam(const std::array<std::pair<int, int>, 2>& tileCells, const Tiles& tiles,
                                         const float seamX, const float seamZ, const bool collisionEnabled)
  {
    constexpr float step = 1.0f / 50.0f;
    constexpr int ticks = 150;

    const auto scene = fixtures::makeScene();
    for (const auto& [row, col] : tileCells)
    {
      const auto terrainObject = fixtures::addObject(scene, "Terrain", basin::tileOrigin(row, col));
      const auto terrainComponent = std::make_shared<Terrain>();
      terrainObject->addComponent(terrainComponent);
      terrainComponent->setVolume(tiles[tileIndex(row, col)]);
      terrainComponent->setCollisionEnabled(collisionEnabled);
    }

    const float groundY = basin::worldGroundY(seamX, seamZ);
    const glm::vec3 spots[3] = { { seamX, groundY + 4.0f, seamZ - 8.0f }, { seamX, groundY + 5.5f, seamZ },
                                 { seamX, groundY + 6.0f, seamZ + 8.0f } };
    const std::string kinds[3] = { "sphere", "d20", "capsule" };
    const std::shared_ptr<Object> objects[3] = {
      scene.objectManager->instantiate(defaultproject::sphere(spots[0])),
      scene.objectManager->instantiate(defaultproject::instanceOf(defaultproject::libraryBody("D20"), spots[1],
                                                                  glm::vec3(1.0f), { 1.5f, 0.0f, 2.0f })),
      scene.objectManager->instantiate(defaultproject::instanceOf(defaultproject::capsuleBody(), spots[2]))
    };

    CollisionSystem collisions;
    std::array<SeamDrop, 3> drops;
    for (int tick = 0; tick < ticks; ++tick)
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, step);
      collisions.fixedUpdate(*scene.objectManager, step);

      for (size_t i = 0; i < 3; ++i)
      {
        const float gap = gapBelow(objects[i], kinds[i]);
        const glm::vec3 position = fixtures::positionOf(objects[i]);
        drops[i].lowestGap = std::min(drops[i].lowestGap, gap);
        const float center = position.y - basin::worldGroundY(position.x, position.z);
        drops[i].lowestCenter = std::min(drops[i].lowestCenter, center);
        drops[i].drift = std::max(drops[i].drift, std::hypot(position.x - spots[i].x, position.z - spots[i].z));
        if (tick >= ticks - 50)
        {
          drops[i].lastSecondGap = std::max(drops[i].lastSecondGap, std::abs(gap));
        }
      }
    }

    for (size_t i = 0; i < 3; ++i)
    {
      drops[i].finalGap = gapBelow(objects[i], kinds[i]);
    }

    return drops;
  }
}

TEST_F(DefaultTerrainTilesTest, BodiesDroppedOnAFlatSeamLandAndStayOnIt)
{
  // The seam between the arena tile and its eastern neighbor, on the arena floor.
  const float seamX = basin::tileOrigin(1, 2).x;
  const float seamZ = 0.0f;

  const auto drops = dropAcrossSeam({ { { 1, 1 }, { 1, 2 } } }, s_tiles, seamX, seamZ, true);
  const char* names[3] = { "sphere", "D20", "capsule" };

  for (size_t i = 0; i < 3; ++i)
  {
    SCOPED_TRACE(names[i]);
    EXPECT_NEAR(drops[i].finalGap, 0.0f, 0.15f);
    EXPECT_LT(drops[i].lastSecondGap, 0.15f);
    EXPECT_GT(drops[i].lowestGap, -0.15f);
    EXPECT_LT(drops[i].drift, 3.0f);
  }

  // Positive control: without the terrain's collision the same bodies fall far below the ground.
  const auto open = dropAcrossSeam({ { { 1, 1 }, { 1, 2 } } }, s_tiles, seamX, seamZ, false);
  for (size_t i = 0; i < 3; ++i)
  {
    SCOPED_TRACE(names[i]);
    EXPECT_LT(open[i].finalGap, -5.0f);
  }
}

TEST_F(DefaultTerrainTilesTest, BodiesRollingAcrossASlopedSeamNeverSinkThroughIt)
{
  // The seam between the two southern tiles of the west and middle columns, on the rim's slope.
  const float seamX = basin::tileOrigin(0, 1).x;
  const float seamZ = basin::tileOrigin(0, 0).z + 60.0f * voxel;

  const auto drops = dropAcrossSeam({ { { 0, 0 }, { 0, 1 } } }, s_tiles, seamX, seamZ, true);
  const char* names[3] = { "sphere", "D20", "capsule" };

  // Every body's center stays above the ground: a sphere's is a radius up, and the others' no lower than half that.
  for (size_t i = 0; i < 3; ++i)
  {
    SCOPED_TRACE(names[i]);
    EXPECT_GT(drops[i].lowestCenter, 0.5f);
  }

  // Positive control: without the terrain's collision the same bodies fall through the slope.
  const auto open = dropAcrossSeam({ { { 0, 0 }, { 0, 1 } } }, s_tiles, seamX, seamZ, false);
  for (size_t i = 0; i < 3; ++i)
  {
    SCOPED_TRACE(names[i]);
    EXPECT_LT(open[i].lowestCenter, -5.0f);
  }
}
