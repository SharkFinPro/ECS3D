#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "DefaultProjectParts.h"
#include "DefaultTerrain.h"
#include "DefaultTerrainTiles.h"
#include "DefaultTerrainTilesFixture.h"
#include "PhysicsSystem.h"
#include "TestScene.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/collisions/ConvexMeshCollider.h"
#include "objects/components/collisions/Terrain.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainVolume.h"

#include <glm/vec3.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <uuid.h>

using basintest::DefaultTerrainTilesTest;
using basintest::Tiles;
using basintest::sameChunks;
using basintest::terrainRecord;
using basintest::tileIndex;

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
  enum class BodyKind {
    sphere,
    d20,
    capsule
  };

  // A dropped body, where it started and what it is.
  struct DroppedBody
  {
    BodyKind kind;
    std::shared_ptr<Object> object;
    glm::vec3 spot;
  };

  struct SeamDrop
  {
    float lowestGap = 1e9f;
    float lowestCenter = 1e9f;
    float lastSecondGap = 0.0f;
    float finalGap = 0.0f;
    float drift = 0.0f;
  };

  struct SeamScene
  {
    fixtures::Scene scene;
    std::vector<DroppedBody> bodies;
  };

  constexpr int dropTicks = 150;
  constexpr float tickSeconds = 1.0f / 50.0f;
  const char* const bodyNames[3] = { "sphere", "D20", "capsule" };

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
  float gapBelow(const DroppedBody& body)
  {
    const glm::vec3 position = fixtures::positionOf(body.object);
    float bottom = position.y - 1.0f;
    if (body.kind == BodyKind::capsule)
    {
      bottom = position.y - 2.0f;
    }
    else if (body.kind == BodyKind::d20)
    {
      bottom = lowestWorldY(*std::dynamic_pointer_cast<ConvexMeshCollider>(
        body.object->getComponent<Collider>(ComponentType::collider)));
    }

    return bottom - basin::worldGroundY(position.x, position.z);
  }

  // The two tiles under a seam, and a sphere, a D20 and an upright capsule three units above the ground over it, a
  // few units apart along z.
  SeamScene buildSeamScene(const std::array<std::pair<int, int>, 2>& tileCells, const Tiles& tiles, const float seamX,
                           const float seamZ, const bool collisionEnabled)
  {
    SeamScene result{ fixtures::makeScene(), {} };
    for (const auto& [row, col] : tileCells)
    {
      const auto terrainObject = fixtures::addObject(result.scene, "Terrain", basin::tileOrigin(row, col));
      const auto terrainComponent = std::make_shared<Terrain>();
      terrainObject->addComponent(terrainComponent);
      terrainComponent->setVolume(tiles[tileIndex(row, col)]);
      terrainComponent->setCollisionEnabled(collisionEnabled);
    }

    const float groundY = basin::worldGroundY(seamX, seamZ);
    const glm::vec3 spots[3] = { { seamX, groundY + 4.0f, seamZ - 8.0f }, { seamX, groundY + 5.5f, seamZ },
                                 { seamX, groundY + 6.0f, seamZ + 8.0f } };
    auto& manager = *result.scene.objectManager;

    result.bodies = {
      { BodyKind::sphere, manager.instantiate(defaultproject::sphere(spots[0])), spots[0] },
      { BodyKind::d20, manager.instantiate(defaultproject::instanceOf(defaultproject::libraryBody("D20"), spots[1],
                                                                       glm::vec3(1.0f), { 1.5f, 0.0f, 2.0f })), spots[1] },
      { BodyKind::capsule, manager.instantiate(defaultproject::instanceOf(defaultproject::capsuleBody(), spots[2])),
        spots[2] }
    };

    return result;
  }

  void recordTick(SeamDrop& drop, const DroppedBody& body, const bool lastSecond)
  {
    const float gap = gapBelow(body);
    const glm::vec3 position = fixtures::positionOf(body.object);
    drop.lowestGap = std::min(drop.lowestGap, gap);
    drop.lowestCenter = std::min(drop.lowestCenter, position.y - basin::worldGroundY(position.x, position.z));
    drop.drift = std::max(drop.drift, std::hypot(position.x - body.spot.x, position.z - body.spot.z));
    if (lastSecond)
    {
      drop.lastSecondGap = std::max(drop.lastSecondGap, std::abs(gap));
    }

    drop.finalGap = gap;
  }

  // Three seconds of the server's ticks over the scene's bodies.
  std::array<SeamDrop, 3> measureDrops(const SeamScene& seamScene)
  {
    CollisionSystem collisions;
    std::array<SeamDrop, 3> drops;

    for (int tick = 0; tick < dropTicks; ++tick)
    {
      PhysicsSystem::fixedUpdate(*seamScene.scene.objectManager, tickSeconds);
      collisions.fixedUpdate(*seamScene.scene.objectManager, tickSeconds);

      for (size_t i = 0; i < drops.size(); ++i)
      {
        recordTick(drops[i], seamScene.bodies[i], tick >= dropTicks - 50);
      }
    }

    return drops;
  }

  std::array<SeamDrop, 3> dropAcrossSeam(const std::array<std::pair<int, int>, 2>& tileCells, const Tiles& tiles,
                                         const float seamX, const float seamZ, const bool collisionEnabled)
  {
    return measureDrops(buildSeamScene(tileCells, tiles, seamX, seamZ, collisionEnabled));
  }
}

TEST_F(DefaultTerrainTilesTest, BodiesDroppedOnAFlatSeamLandAndStayOnIt)
{
  // The seam between the arena tile and its eastern neighbor, on the arena floor.
  const float seamX = basin::tileOrigin(1, 2).x;
  const float seamZ = 0.0f;

  const auto drops = dropAcrossSeam({ { { 1, 1 }, { 1, 2 } } }, s_tiles, seamX, seamZ, true);

  for (size_t i = 0; i < drops.size(); ++i)
  {
    SCOPED_TRACE(bodyNames[i]);
    EXPECT_NEAR(drops[i].finalGap, 0.0f, 0.15f);
    EXPECT_LT(drops[i].lastSecondGap, 0.15f);
    EXPECT_GT(drops[i].lowestGap, -0.15f);
    EXPECT_LT(drops[i].drift, 3.0f);
  }

  // Positive control: without the terrain's collision the same bodies fall far below the ground.
  const auto open = dropAcrossSeam({ { { 1, 1 }, { 1, 2 } } }, s_tiles, seamX, seamZ, false);
  for (size_t i = 0; i < open.size(); ++i)
  {
    SCOPED_TRACE(bodyNames[i]);
    EXPECT_LT(open[i].finalGap, -5.0f);
  }
}

TEST_F(DefaultTerrainTilesTest, BodiesRollingAcrossASlopedSeamNeverSinkThroughIt)
{
  // The seam between the two southern tiles of the west and middle columns, on the rim's slope.
  const float seamX = basin::tileOrigin(0, 1).x;
  const float seamZ = basin::tileOrigin(0, 0).z + 60.0f * basin::voxelSize;

  const auto drops = dropAcrossSeam({ { { 0, 0 }, { 0, 1 } } }, s_tiles, seamX, seamZ, true);

  // Every body's center stays above the ground: a sphere's is a radius up, and the others' no lower than half that.
  for (size_t i = 0; i < drops.size(); ++i)
  {
    SCOPED_TRACE(bodyNames[i]);
    EXPECT_GT(drops[i].lowestCenter, 0.5f);
  }

  // Positive control: without the terrain's collision the same bodies fall through the slope.
  const auto open = dropAcrossSeam({ { { 0, 0 }, { 0, 1 } } }, s_tiles, seamX, seamZ, false);
  for (size_t i = 0; i < open.size(); ++i)
  {
    SCOPED_TRACE(bodyNames[i]);
    EXPECT_LT(open[i].lowestCenter, -5.0f);
  }
}
