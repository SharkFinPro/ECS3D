#include <gtest/gtest.h>

#include "TestScene.h"
#include "objects/Object.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/Terrain.h"
#include "queries/SceneQueries.h"
#include "terrain/TerrainGenerators.h"
#include "terrain/TerrainOps.h"
#include "terrain/TerrainVolume.h"

#include <glm/vec3.hpp>
#include <cstdint>
#include <memory>
#include <vector>
#include <uuid.h>

namespace {
  using fixtures::makeScene;
  using fixtures::Scene;

  constexpr float voxelSize = 2.0f;
  constexpr float tolerance = 1e-3f;
  constexpr float groundHeight = 10.0f;
  constexpr uint32_t allLayers = 0xFFFFFFFFu;
  constexpr uint32_t terrainLayer = 3;

  // Flat ground at local y = 10 over x and z in [0, 126], thick enough to reach below any query here.
  std::shared_ptr<terrain::TerrainVolume> makeGround()
  {
    auto volume = std::make_shared<terrain::TerrainVolume>(voxelSize);
    terrain::commit(*volume, terrain::fillFlat(*volume, { 64, 64 }, groundHeight, 20.0f, 4));

    return volume;
  }

  struct Ground {
    std::shared_ptr<terrain::TerrainVolume> volume;
    std::shared_ptr<Object> object;
    std::shared_ptr<Terrain> terrain;
  };

  Ground addGround(const Scene& scene, const glm::vec3& position = glm::vec3(0))
  {
    Ground ground;
    ground.volume = makeGround();
    ground.object = fixtures::addObject(scene, "Ground", position);
    ground.terrain = std::make_shared<Terrain>();
    ground.object->addComponent(ground.terrain);
    ground.terrain->setVolume(ground.volume);
    ground.terrain->setLayer(terrainLayer);

    return ground;
  }

  struct Hit {
    uuids::uuid object;
    glm::vec3 point{ 0.0f };
    glm::vec3 normal{ 0.0f };
    float distance = 0.0f;
  };

  bool castRay(const Scene& scene, const glm::vec3& origin, const glm::vec3& direction, const float maxDistance,
               Hit& hit, const uint32_t layerMask = allLayers, const uuids::uuid& ignoreObject = {})
  {
    return SceneQueries::raycast(*scene.objectManager, origin, direction, maxDistance, layerMask, ignoreObject,
                                 hit.object, hit.point, hit.normal, hit.distance);
  }

  std::vector<uuids::uuid> overlaps(const Scene& scene, const glm::vec3& center, const float radius,
                                    const uint32_t layerMask = allLayers, const uuids::uuid& ignoreObject = {})
  {
    std::vector<uuids::uuid> results;
    SceneQueries::overlapSphere(*scene.objectManager, center, radius, layerMask, ignoreObject, results);

    return results;
  }

  void expectNear(const glm::vec3& actual, const glm::vec3& expected)
  {
    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
  }
}

TEST(TerrainSceneQueryTest, ARayDownHitsTheSurfaceAndOneUpMisses)
{
  const auto scene = makeScene();
  const auto ground = addGround(scene);

  Hit hit;
  ASSERT_TRUE(castRay(scene, { 30, 50, 30 }, { 0, -1, 0 }, 100.0f, hit));
  EXPECT_EQ(hit.object, ground.object->getUUID());
  EXPECT_NEAR(hit.distance, 40.0f, tolerance);
  expectNear(hit.point, { 30, groundHeight, 30 });
  expectNear(hit.normal, { 0, 1, 0 });

  Hit miss;
  EXPECT_FALSE(castRay(scene, { 30, 50, 30 }, { 0, 1, 0 }, 100.0f, miss));
}

TEST(TerrainSceneQueryTest, TheRayLengthDecidesWhetherItReachesTheGround)
{
  const auto scene = makeScene();
  const auto ground = addGround(scene);

  Hit hit;
  EXPECT_FALSE(castRay(scene, { 30, 50, 30 }, { 0, -1, 0 }, 39.0f, hit));
  EXPECT_TRUE(castRay(scene, { 30, 50, 30 }, { 0, -1, 0 }, 41.0f, hit));
  EXPECT_EQ(hit.object, ground.object->getUUID());
  EXPECT_NEAR(hit.distance, 40.0f, tolerance);
}

TEST(TerrainSceneQueryTest, ADirectionThatIsNotNormalizedStillGivesWorldDistance)
{
  const auto scene = makeScene();
  const auto ground = addGround(scene);

  Hit hit;
  ASSERT_TRUE(castRay(scene, { 30, 50, 30 }, { 0, -5, 0 }, 100.0f, hit));
  EXPECT_EQ(hit.object, ground.object->getUUID());
  EXPECT_NEAR(hit.distance, 40.0f, tolerance);
}

TEST(TerrainSceneQueryTest, ARotatedAndTranslatedTerrainIsHitAlongItsRotatedDown)
{
  const auto scene = makeScene();
  const auto ground = addGround(scene, { 100, 0, 0 });
  fixtures::transformOf(ground.object)->setRotation({ 0, 0, 90 });

  // A quarter turn about z sends local up (0, 1, 0) to world (-1, 0, 0), so the surface is the plane x = 90.
  Hit hit;
  ASSERT_TRUE(castRay(scene, { 50, 30, 30 }, { 1, 0, 0 }, 100.0f, hit));
  EXPECT_EQ(hit.object, ground.object->getUUID());
  EXPECT_NEAR(hit.distance, 40.0f, tolerance);
  expectNear(hit.point, { 90, 30, 30 });
  expectNear(hit.normal, { -1, 0, 0 });

  // The unrotated world "down" no longer meets the ground.
  Hit miss;
  EXPECT_FALSE(castRay(scene, { 30, 50, 30 }, { 0, -1, 0 }, 100.0f, miss));
}

TEST(TerrainSceneQueryTest, LayerMaskAndIgnoreObjectApplyToTerrain)
{
  const auto scene = makeScene();
  const auto ground = addGround(scene);

  Hit hit;
  EXPECT_TRUE(castRay(scene, { 30, 50, 30 }, { 0, -1, 0 }, 100.0f, hit, 1u << terrainLayer));
  EXPECT_EQ(hit.object, ground.object->getUUID());

  EXPECT_FALSE(castRay(scene, { 30, 50, 30 }, { 0, -1, 0 }, 100.0f, hit, allLayers & ~(1u << terrainLayer)));
  EXPECT_FALSE(castRay(scene, { 30, 50, 30 }, { 0, -1, 0 }, 100.0f, hit, allLayers, ground.object->getUUID()));
}

TEST(TerrainSceneQueryTest, TheNearerOfASphereAndTheTerrainWins)
{
  const auto scene = makeScene();
  const auto ground = addGround(scene);

  const auto sphere = fixtures::addObject(scene, "Sphere", { 30, 25, 30 });
  fixtures::addSphereCollider(sphere, 2.0f);

  Hit hit;
  ASSERT_TRUE(castRay(scene, { 30, 50, 30 }, { 0, -1, 0 }, 100.0f, hit));
  EXPECT_EQ(hit.object, sphere->getUUID());
  EXPECT_NEAR(hit.distance, 23.0f, tolerance);

  // A sphere buried below the surface is farther along the ray than the ground.
  fixtures::transformOf(sphere)->setPosition({ 30, -5, 30 });
  ASSERT_TRUE(castRay(scene, { 30, 50, 30 }, { 0, -1, 0 }, 100.0f, hit));
  EXPECT_EQ(hit.object, ground.object->getUUID());
  EXPECT_NEAR(hit.distance, 40.0f, tolerance);
}

TEST(TerrainSceneQueryTest, ASphereTouchingTheGroundOverlapsItAndOneAboveDoesNot)
{
  const auto scene = makeScene();
  const auto ground = addGround(scene);

  const auto touching = overlaps(scene, { 30, 12, 30 }, 3.0f);
  ASSERT_EQ(touching.size(), 1u);
  EXPECT_EQ(touching.front(), ground.object->getUUID());

  EXPECT_TRUE(overlaps(scene, { 30, 14, 30 }, 3.0f).empty());
  EXPECT_TRUE(overlaps(scene, { 30, 20, 30 }, 3.0f).empty());
}

TEST(TerrainSceneQueryTest, LayerMaskAndIgnoreObjectExcludeTerrainFromAnOverlap)
{
  const auto scene = makeScene();
  const auto ground = addGround(scene);
  const glm::vec3 center(30, 12, 30);

  EXPECT_EQ(overlaps(scene, center, 3.0f, 1u << terrainLayer).size(), 1u);
  EXPECT_TRUE(overlaps(scene, center, 3.0f, allLayers & ~(1u << terrainLayer)).empty());
  EXPECT_TRUE(overlaps(scene, center, 3.0f, allLayers, ground.object->getUUID()).empty());
}

TEST(TerrainSceneQueryTest, ALargeSphereOverlapsGroundBeyondTheBandOnlyWithinItsRadius)
{
  const auto scene = makeScene();
  const auto ground = addGround(scene);

  // The band is 8 units, so both centers read it as the field's limit; only the ground within reach counts.
  const auto reaching = overlaps(scene, { 30, 25, 30 }, 20.0f);
  ASSERT_EQ(reaching.size(), 1u);
  EXPECT_EQ(reaching.front(), ground.object->getUUID());

  EXPECT_TRUE(overlaps(scene, { 30, 40, 30 }, 20.0f).empty());
}

TEST(TerrainSceneQueryTest, ATerrainWithoutCollisionIsNotQueried)
{
  const auto scene = makeScene();
  const auto ground = addGround(scene);

  Hit hit;
  ASSERT_TRUE(castRay(scene, { 30, 50, 30 }, { 0, -1, 0 }, 100.0f, hit));
  EXPECT_EQ(overlaps(scene, { 30, 12, 30 }, 3.0f).size(), 1u);

  ground.terrain->setCollisionEnabled(false);
  EXPECT_FALSE(castRay(scene, { 30, 50, 30 }, { 0, -1, 0 }, 100.0f, hit));
  EXPECT_TRUE(overlaps(scene, { 30, 12, 30 }, 3.0f).empty());
}

TEST(TerrainSceneQueryTest, AGroundBetweenSamplePlanesIsOverlappedByASphereOnlyWithinItsRadius)
{
  const auto scene = makeScene();
  auto ground = addGround(scene);
  terrain::commit(*ground.volume, terrain::fillFlat(*ground.volume, { 64, 64 }, 11.0f, 20.0f, 4));

  // The surface at y = 11 is off the sample planes. A center 7.5 above it reaches it with 7.8 and not with 7.0.
  const auto reaching = overlaps(scene, { 30, 18.5f, 30 }, 7.8f);
  ASSERT_EQ(reaching.size(), 1u);
  EXPECT_EQ(reaching.front(), ground.object->getUUID());

  EXPECT_TRUE(overlaps(scene, { 30, 18.5f, 30 }, 7.0f).empty());
}

TEST(TerrainSceneQueryTest, ATerrainWithNoVolumeIsHitByNeitherQuery)
{
  const auto scene = makeScene();
  const auto bare = fixtures::addObject(scene, "Bare", glm::vec3(0));
  bare->addComponent(std::make_shared<Terrain>());

  const auto sphere = fixtures::addObject(scene, "Sphere", { 30, 12, 30 });
  fixtures::addSphereCollider(sphere, 2.0f);

  Hit hit;
  ASSERT_TRUE(castRay(scene, { 30, 50, 30 }, { 0, -1, 0 }, 100.0f, hit));
  EXPECT_EQ(hit.object, sphere->getUUID());

  const auto touching = overlaps(scene, { 30, 12, 30 }, 3.0f);
  ASSERT_EQ(touching.size(), 1u);
  EXPECT_EQ(touching.front(), sphere->getUUID());
}

TEST(TerrainSceneQueryTest, ATerrainWhoseVolumeExpiredIsHitByNeitherQuery)
{
  const auto scene = makeScene();
  auto ground = addGround(scene);

  Hit hit;
  ASSERT_TRUE(castRay(scene, { 30, 50, 30 }, { 0, -1, 0 }, 100.0f, hit));
  ASSERT_EQ(overlaps(scene, { 30, 12, 30 }, 3.0f).size(), 1u);

  ground.volume.reset();
  EXPECT_FALSE(castRay(scene, { 30, 50, 30 }, { 0, -1, 0 }, 100.0f, hit));
  EXPECT_TRUE(overlaps(scene, { 30, 12, 30 }, 3.0f).empty());
}

TEST(TerrainSceneQueryTest, ARayStartingInsideSolidGroundHitsAtDistanceZero)
{
  const auto scene = makeScene();
  const auto ground = addGround(scene);

  Hit hit;
  ASSERT_TRUE(castRay(scene, { 30, 5, 30 }, { 0, -1, 0 }, 100.0f, hit));
  EXPECT_EQ(hit.object, ground.object->getUUID());
  EXPECT_NEAR(hit.distance, 0.0f, tolerance);
}
