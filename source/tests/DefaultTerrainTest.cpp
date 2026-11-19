#include <gtest/gtest.h>

#include "DefaultProjectParts.h"
#include "DefaultTerrain.h"
#include "assets/AssetRegistry.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainRaycast.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <glm/vec3.hpp>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <uuid.h>

namespace {
  const auto showcaseUuid = uuids::uuid::from_string(defaultproject::showcaseTerrain).value();
  const auto otherUuid = uuids::uuid::from_string("11111111-1111-1111-1111-111111111111").value();

  AssetRecord terrainRecord(const uuids::uuid& uuid)
  {
    return { .uuid = uuid, .type = AssetType::Terrain, .path = terrainAssetPath(uuid) };
  }

  bool solidAt(const terrain::TerrainVolume& volume, const int x, const int y, const int z)
  {
    return volume.distanceAtSample({ x, y, z }) < 0;
  }

  bool airAt(const terrain::TerrainVolume& volume, const int x, const int y, const int z)
  {
    return volume.distanceAtSample({ x, y, z }) > 0;
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
  std::set<uint8_t> solidSurfaceMaterials(const terrain::TerrainVolume& volume)
  {
    std::set<uint8_t> materials;

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

    return materials;
  }

  class DefaultTerrainTest : public testing::Test
  {
  protected:
    static void SetUpTestSuite()
    {
      s_full = std::make_unique<terrain::TerrainVolume>(showcase::voxelSize);
      const auto start = std::chrono::steady_clock::now();
      generateShowcaseTerrain(*s_full);
      s_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

      s_ground = std::make_unique<terrain::TerrainVolume>(showcase::voxelSize);
      generateShowcaseGround(*s_ground);
    }

    static void TearDownTestSuite()
    {
      s_full.reset();
      s_ground.reset();
    }

    static std::unique_ptr<terrain::TerrainVolume> s_full;
    static std::unique_ptr<terrain::TerrainVolume> s_ground;
    static double s_seconds;
  };

  std::unique_ptr<terrain::TerrainVolume> DefaultTerrainTest::s_full;
  std::unique_ptr<terrain::TerrainVolume> DefaultTerrainTest::s_ground;
  double DefaultTerrainTest::s_seconds = 0.0;
}

TEST_F(DefaultTerrainTest, TheChunkCountStaysWithinWhatEveryJoinStreams)
{
  EXPECT_GE(s_full->chunks().size(), 300u);
  EXPECT_LE(s_full->chunks().size(), 700u);
}

TEST_F(DefaultTerrainTest, GenerationStaysQuick)
{
#ifndef NDEBUG
  GTEST_SKIP() << "A Release measurement: Debug, sanitizer and coverage builds skip it";
#endif
  std::cout << "[ INFO ] showcase generation took " << s_seconds << " s for " << s_full->chunks().size() << " chunks\n";
  EXPECT_LT(s_seconds, 5.0);
}

TEST_F(DefaultTerrainTest, TheTunnelIsAirBetweenSolidRockAboveAndBelow)
{
  const int x = showcase::tunnelX;
  const int z = (showcase::tunnelStartZ + showcase::tunnelEndZ) / 2;

  EXPECT_TRUE(airAt(*s_full, x, showcase::tunnelY, z));
  EXPECT_TRUE(solidAt(*s_full, x, showcase::tunnelY + showcase::tunnelRadius + 10, z));
  EXPECT_TRUE(solidAt(*s_full, x, showcase::tunnelY - showcase::tunnelRadius - 6, z));

  // Positive control: the ground alone is solid through the same column.
  EXPECT_TRUE(solidAt(*s_ground, x, showcase::tunnelY, z));
  EXPECT_TRUE(solidAt(*s_ground, x, showcase::tunnelY + showcase::tunnelRadius + 10, z));
  EXPECT_TRUE(solidAt(*s_ground, x, showcase::tunnelY - showcase::tunnelRadius - 6, z));
}

TEST_F(DefaultTerrainTest, TheTunnelOpensUnderTheRidgeAtBothEnds)
{
  // Both ends of the stretch where the ridge stands above the roof.
  for (const int z : { 132, 188 })
  {
    EXPECT_TRUE(airAt(*s_full, showcase::tunnelX, showcase::tunnelY, z)) << z;
    EXPECT_TRUE(solidAt(*s_ground, showcase::tunnelX, showcase::tunnelY, z)) << z;
  }

  // Both mouths open onto the ground outside the ridge.
  EXPECT_TRUE(airAt(*s_full, showcase::tunnelX, showcase::tunnelY, showcase::tunnelStartZ - 12));
  EXPECT_TRUE(airAt(*s_full, showcase::tunnelX, showcase::tunnelY, showcase::tunnelEndZ + 12));
}

TEST_F(DefaultTerrainTest, TheArchSpansAnOpeningUnderARockSpan)
{
  const int x = showcase::archX;
  const int z = showcase::archZ;
  const int apex = showcase::groundHeight + showcase::archRingRadius;

  EXPECT_TRUE(solidAt(*s_full, x, showcase::groundHeight - 10, z));
  EXPECT_TRUE(airAt(*s_full, x, showcase::groundHeight + 8, z));
  EXPECT_TRUE(solidAt(*s_full, x, apex, z));
  EXPECT_TRUE(airAt(*s_full, x, apex + showcase::archTubeRadius + 6, z));

  // Positive control: the ground alone has no span.
  EXPECT_TRUE(airAt(*s_ground, x, showcase::groundHeight + 8, z));
  EXPECT_TRUE(airAt(*s_ground, x, apex, z));
}

TEST_F(DefaultTerrainTest, TheOverhangHangsOverOpenAir)
{
  const int x = showcase::overhangX;
  const int z = showcase::overhangZ;

  EXPECT_TRUE(solidAt(*s_full, x, showcase::groundHeight - 10, z));
  EXPECT_TRUE(airAt(*s_full, x, showcase::groundHeight + 15, z));
  EXPECT_TRUE(solidAt(*s_full, x, showcase::overhangY, z));
  EXPECT_TRUE(airAt(*s_full, x, showcase::overhangY + 15, z));

  // Positive control: the ground alone is open air at the overhang's height.
  EXPECT_TRUE(airAt(*s_ground, x, showcase::overhangY, z));
}

TEST_F(DefaultTerrainTest, TheIslandFloatsAboveClearAir)
{
  const int x = showcase::islandX;
  const int z = showcase::islandZ;

  // The hills can dip well below the base ground height here, so the ground probe sits near the bottom.
  EXPECT_TRUE(solidAt(*s_full, x, 4, z));
  EXPECT_TRUE(airAt(*s_full, x, 75, z));
  EXPECT_TRUE(solidAt(*s_full, x, showcase::islandY, z));
  EXPECT_TRUE(airAt(*s_full, x, showcase::islandY + 20, z));

  // Positive control: the ground alone is open air where the island floats.
  EXPECT_TRUE(airAt(*s_ground, x, showcase::islandY, z));
}

TEST_F(DefaultTerrainTest, TheSurfaceUsesSeveralMaterials)
{
  const auto materials = solidSurfaceMaterials(*s_full);

  EXPECT_GE(materials.size(), 4u);
  EXPECT_TRUE(materials.contains(0));
  EXPECT_TRUE(materials.contains(1));
  EXPECT_TRUE(materials.contains(2));
  EXPECT_TRUE(materials.contains(4));
  EXPECT_FALSE(materials.contains(terrain::reservedMaterial));

  // Positive control: the ground alone is one material.
  EXPECT_EQ(solidSurfaceMaterials(*s_ground).size(), 1u);
}

TEST_F(DefaultTerrainTest, InstallFillsTheVolumeDeterministicallyAndRecordsWhatItHolds)
{
  AssetRegistry registry;
  registry.registerAsset(terrainRecord(showcaseUuid));

  terrain::TerrainStore store;
  store.reconcile(registry);
  ASSERT_TRUE(store.find(showcaseUuid)->chunks().empty());
  ASSERT_NE(store.description(showcaseUuid)->voxelSize, showcase::voxelSize);

  installDefaultTerrains(store);

  const auto volume = store.find(showcaseUuid);
  ASSERT_NE(volume, nullptr);
  ASSERT_FALSE(volume->chunks().empty());

  // A second, separate generation matches the suite's shared one, and the comparison tells different volumes apart.
  EXPECT_TRUE(sameChunks(*volume, *s_full));
  EXPECT_FALSE(sameChunks(*volume, *s_ground));

  const auto* description = store.description(showcaseUuid);
  ASSERT_NE(description, nullptr);
  EXPECT_EQ(description->chunkCount, volume->chunks().size());
  EXPECT_FLOAT_EQ(description->voxelSize, showcase::voxelSize);
  EXPECT_FLOAT_EQ(volume->voxelSize(), showcase::voxelSize);
  EXPECT_EQ(store.find(showcaseUuid), volume);
  EXPECT_FALSE(terrain::firstIncompleteTerrain(store).has_value());

  for (const auto& [coord, chunk] : volume->chunks())
  {
    EXPECT_GT(store.revision(showcaseUuid, coord), 0u);
  }
}

TEST_F(DefaultTerrainTest, TheOriginPutsTheLandscapeCenterAndTheBaseGroundAtTheWorldOrigin)
{
  const glm::vec3 origin = showcase::terrainOrigin();
  const float centerColumn = static_cast<float>(showcase::columns / 2) * showcase::voxelSize;

  EXPECT_NEAR(origin.x + centerColumn, 0.0f, 1e-3f);
  EXPECT_NEAR(origin.z + centerColumn, 0.0f, 1e-3f);
  EXPECT_NEAR(origin.y + static_cast<float>(showcase::groundHeight) * showcase::voxelSize, 0.0f, 1e-3f);

  // The tunnel's column is the center column, so the corridor runs through x = 0.
  EXPECT_EQ(showcase::tunnelX, showcase::columns / 2);
}

TEST_F(DefaultTerrainTest, ThePlayerSpawnStandsJustAboveTheGroundSouthOfTheTunnel)
{
  const glm::vec3 spawn = showcase::playerSpawn();
  const float bottom = spawn.y - 1.0f;

  const auto hit = terrain::raycast(*s_full, spawn - showcase::terrainOrigin(), { 0.0f, -1.0f, 0.0f }, 100.0f);
  ASSERT_TRUE(hit.has_value());

  const float gap = bottom - (hit->point.y + showcase::terrainOrigin().y);
  EXPECT_GE(gap, 0.0f);
  EXPECT_LE(gap, 3.0f);

  // South of the tunnel's south mouth, along its axis.
  const float spawnZVoxel = (spawn.z - showcase::terrainOrigin().z) / showcase::voxelSize;
  EXPECT_LT(spawnZVoxel, static_cast<float>(showcase::tunnelStartZ));
  EXPECT_NEAR((spawn.x - showcase::terrainOrigin().x) / showcase::voxelSize, static_cast<float>(showcase::tunnelX),
              1e-3f);

  // Positive control: a ray from far above the ridge lands well higher than the spawn's ground.
  const auto ridge = terrain::raycast(*s_full, glm::vec3(160.0f, 200.0f, 160.0f) * showcase::voxelSize,
                                      { 0.0f, -1.0f, 0.0f }, 1000.0f);
  ASSERT_TRUE(ridge.has_value());
  EXPECT_GT(ridge->point.y + showcase::terrainOrigin().y, spawn.y + 10.0f);
}

TEST_F(DefaultTerrainTest, InstallLeavesOtherTerrainsAndAnEmptyStoreAlone)
{
  AssetRegistry registry;
  registry.registerAsset(terrainRecord(otherUuid));

  terrain::TerrainStore store;
  store.reconcile(registry);

  installDefaultTerrains(store);

  const auto volume = store.find(otherUuid);
  ASSERT_NE(volume, nullptr);
  EXPECT_TRUE(volume->chunks().empty());
  EXPECT_EQ(store.description(otherUuid)->chunkCount, 0u);
  EXPECT_EQ(store.find(showcaseUuid), nullptr);

  terrain::TerrainStore empty;
  installDefaultTerrains(empty);
  EXPECT_TRUE(empty.terrains().empty());
}
