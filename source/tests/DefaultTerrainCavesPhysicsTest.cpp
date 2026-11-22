#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "DefaultProjectParts.h"
#include "DefaultTerrainCaves.h"
#include "DefaultTerrainTiles.h"
#include "DefaultTerrainTilesFixture.h"
#include "PhysicsSystem.h"
#include "TestScene.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/collisions/Terrain.h"

#include <glm/vec3.hpp>
#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>

using basintest::DefaultTerrainTilesTest;
using basintest::tileIndex;

namespace {
  constexpr float tickSeconds = 1.0f / 50.0f;

  // The three tiles the caves lie in, as Terrain objects of one scene.
  fixtures::Scene buildCaveScene(const basintest::Tiles& tiles, const bool collisionEnabled)
  {
    auto scene = fixtures::makeScene();
    for (const auto& [row, col] : { std::pair{ 2, 1 }, { 1, 2 }, { 1, 1 } })
    {
      const auto object = fixtures::addObject(scene, "Terrain", basin::tileOrigin(row, col));
      const auto terrain = std::make_shared<Terrain>();
      object->addComponent(terrain);
      terrain->setVolume(tiles[tileIndex(row, col)]);
      terrain->setCollisionEnabled(collisionEnabled);
    }

    return scene;
  }

  // A sphere released at `start` and stepped as the server steps for `seconds`.
  glm::vec3 dropSphere(const basintest::Tiles& tiles, const glm::vec3& start, const float seconds,
                       const bool collisionEnabled)
  {
    auto scene = buildCaveScene(tiles, collisionEnabled);
    const auto sphere = scene.objectManager->instantiate(defaultproject::sphere(start));
    CollisionSystem collisions;

    for (int tick = 0; tick < static_cast<int>(seconds / tickSeconds); ++tick)
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, tickSeconds);
      collisions.fixedUpdate(*scene.objectManager, tickSeconds);
    }

    return fixtures::positionOf(sphere);
  }
}

TEST_F(DefaultTerrainTilesTest, ASphereDroppedOverEachStackSettlesOnItsSurface)
{
  for (const auto& stack : basin::caves::stacks())
  {
    SCOPED_TRACE(stack.name);
    const glm::vec3 start(stack.worldX, stack.surfaceY + basin::caves::stackLift, stack.worldZ);

    const glm::vec3 landed = dropSphere(s_tiles, start, 3.0f, true);
    EXPECT_NEAR(landed.y, stack.surfaceY + 1.0f, 1.3f);
    EXPECT_LT(std::hypot(landed.x - start.x, landed.z - start.z), 4.0f);

    // Positive control: without the terrain's collision it falls out of the world.
    EXPECT_LT(dropSphere(s_tiles, start, 3.0f, false).y, stack.surfaceY - 20.0f);
  }
}

TEST_F(DefaultTerrainTilesTest, ASphereDroppedDownTheEastShaftLandsInTheCavernAndRollsOffTheRamp)
{
  // Over the shaft, well above the plateau.
  const float columnX = 346.0f;
  const float columnZ = 200.0f;
  const glm::vec3 origin = basin::tileOrigin(0, 0);
  const glm::vec3 start(columnX * basin::voxelSize + origin.x, 75.0f, columnZ * basin::voxelSize + origin.z);

  const glm::vec3 landed = dropSphere(s_tiles, start, 8.0f, true);

  // It comes to rest on the cavern's level floor, west of the shaft, still under the plateau.
  const float rampFootX = 338.0f * basin::voxelSize + origin.x;
  EXPECT_LT(landed.x, rampFootX + 1.0f);
  EXPECT_GT(landed.x, 305.0f * basin::voxelSize + origin.x);
  EXPECT_NEAR(landed.y, 1.0f, 1.3f);

  // Positive control: without the terrain's collision it just falls out of the world.
  EXPECT_LT(dropSphere(s_tiles, start, 8.0f, false).y, -20.0f);
}
