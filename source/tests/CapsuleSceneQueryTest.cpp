#include <gtest/gtest.h>

#include "TestPrinters.h"
#include "TestScene.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Transform.h"
#include "queries/SceneQueries.h"

#include <glm/vec3.hpp>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>
#include <uuid.h>

namespace {
  constexpr uint32_t allLayers = 0xFFFFFFFFu;
  constexpr float tolerance = 1e-4f;

  using fixtures::addObject;

  struct Hit {
    uuids::uuid object;
    glm::vec3 point{ 0.0f };
    glm::vec3 normal{ 0.0f };
    float distance = 0.0f;
  };

  // A capsule of radius 0.5 and total height 2 at the origin: a core segment from y = -0.5 to 0.5.
  std::shared_ptr<Object> addCapsule(const fixtures::Scene& scene, const glm::vec3& rotation = glm::vec3(0))
  {
    const auto object = addObject(scene, "Capsule", { 0, 0, 0 });
    fixtures::transformOf(object)->setRotation(rotation);
    fixtures::addCapsuleCollider(object, 0.5f, 2.0f);

    return object;
  }

  bool castRay(const fixtures::Scene& scene, const glm::vec3& origin, const glm::vec3& direction,
               const float maxDistance, Hit& hit)
  {
    return SceneQueries::raycast(*scene.objectManager, origin, direction, maxDistance, allLayers, {},
                                 hit.object, hit.point, hit.normal, hit.distance);
  }

  std::vector<uuids::uuid> overlaps(const fixtures::Scene& scene, const glm::vec3& center, const float radius)
  {
    std::vector<uuids::uuid> results;
    SceneQueries::overlapSphere(*scene.objectManager, center, radius, allLayers, {}, results);

    return results;
  }

  void expectNear(const char* what, const glm::vec3& actual, const glm::vec3& expected)
  {
    fixtures::expectNear(what, actual, expected, tolerance);
  }
}

TEST(CapsuleSceneQuery, ARayAcrossTheShaftHitsTheSideWithAnOutwardNormal)
{
  const auto scene = fixtures::makeScene();
  const auto capsule = addCapsule(scene);

  Hit hit;
  ASSERT_TRUE(castRay(scene, { 5, 0, 0 }, { -1, 0, 0 }, 100.0f, hit));

  EXPECT_EQ(hit.object, capsule->getUUID());
  EXPECT_NEAR(hit.distance, 4.5f, tolerance);
  expectNear("point", hit.point, { 0.5f, 0, 0 });
  expectNear("normal", hit.normal, { 1, 0, 0 });
}

TEST(CapsuleSceneQuery, ARayDownTheAxisHitsTheCapAtTheTop)
{
  const auto scene = fixtures::makeScene();
  addCapsule(scene);

  Hit hit;
  ASSERT_TRUE(castRay(scene, { 0, 5, 0 }, { 0, -1, 0 }, 100.0f, hit));

  EXPECT_NEAR(hit.distance, 4.0f, tolerance);
  expectNear("point", hit.point, { 0, 1, 0 });
  expectNear("normal", hit.normal, { 0, 1, 0 });
}

TEST(CapsuleSceneQuery, ARayBesideTheCapHitsTheEndSphereRatherThanTheShaft)
{
  const auto scene = fixtures::makeScene();
  addCapsule(scene);

  // Level with y = 0.8, above the shaft's end at 0.5: the cap's sphere is hit at x = 0.4.
  Hit hit;
  ASSERT_TRUE(castRay(scene, { 5, 0.8f, 0 }, { -1, 0, 0 }, 100.0f, hit));

  EXPECT_NEAR(hit.distance, 4.6f, tolerance);
  expectNear("point", hit.point, { 0.4f, 0.8f, 0 });
  expectNear("normal", hit.normal, { 0.8f, 0.6f, 0 });
}

TEST(CapsuleSceneQuery, ARayAboveTheCapOrShortOfItMissesWhileOneAtItHits)
{
  const auto scene = fixtures::makeScene();
  addCapsule(scene);

  Hit hit;
  EXPECT_FALSE(castRay(scene, { 5, 1.2f, 0 }, { -1, 0, 0 }, 100.0f, hit));
  EXPECT_FALSE(castRay(scene, { 5, 0, 0 }, { -1, 0, 0 }, 4.0f, hit));
  EXPECT_FALSE(castRay(scene, { 5, 0, 0 }, { 1, 0, 0 }, 100.0f, hit));

  // Positive control: the same origin, long enough and aimed at it.
  EXPECT_TRUE(castRay(scene, { 5, 0, 0 }, { -1, 0, 0 }, 5.0f, hit));
}

TEST(CapsuleSceneQuery, ARayStartedInsideReportsContactAtItsOrigin)
{
  const auto scene = fixtures::makeScene();
  addCapsule(scene);

  Hit hit;
  ASSERT_TRUE(castRay(scene, { 0, 0, 0 }, { 1, 0, 0 }, 100.0f, hit));

  EXPECT_NEAR(hit.distance, 0.0f, tolerance);
}

TEST(CapsuleSceneQuery, ATurnedCapsuleIsHitWhereItNowLies)
{
  const auto scene = fixtures::makeScene();
  addCapsule(scene, { 0, 0, 90 });

  // Lying along x, so its side is now met from above, and a ray down the old axis meets the side too.
  Hit hit;
  ASSERT_TRUE(castRay(scene, { 0, 5, 0 }, { 0, -1, 0 }, 100.0f, hit));
  EXPECT_NEAR(hit.distance, 4.5f, tolerance);
  expectNear("normal", hit.normal, { 0, 1, 0 });

  ASSERT_TRUE(castRay(scene, { 5, 0, 0 }, { -1, 0, 0 }, 100.0f, hit));
  EXPECT_NEAR(hit.distance, 4.0f, tolerance);
  expectNear("normal", hit.normal, { 1, 0, 0 });
}

TEST(CapsuleSceneQuery, TheNearestOfACapsuleAndABoxWins)
{
  const auto scene = fixtures::makeScene();
  const auto capsule = addCapsule(scene);
  const auto box = fixtures::addBoxCollider(addObject(scene, "Box", { 10, 0, 0 }));

  Hit hit;
  ASSERT_TRUE(castRay(scene, { 20, 0, 0 }, { -1, 0, 0 }, 100.0f, hit));
  EXPECT_EQ(hit.object, box->getOwner()->getUUID());
  EXPECT_NEAR(hit.distance, 9.0f, tolerance);

  // From the other side the capsule is in front of the box.
  ASSERT_TRUE(castRay(scene, { -5, 0, 0 }, { 1, 0, 0 }, 100.0f, hit));
  EXPECT_EQ(hit.object, capsule->getUUID());
}

TEST(CapsuleSceneQuery, ASphereOverlapsWithinTheSumOfRadiiOfTheSegment)
{
  const auto scene = fixtures::makeScene();
  const auto capsule = addCapsule(scene);

  EXPECT_EQ(overlaps(scene, { 0.9f, 0, 0 }, 0.5f), std::vector<uuids::uuid>{ capsule->getUUID() });
  EXPECT_EQ(overlaps(scene, { 0, 1.4f, 0 }, 0.5f), std::vector<uuids::uuid>{ capsule->getUUID() });
  EXPECT_EQ(overlaps(scene, { 0.4f, -0.2f, 0 }, 0.1f), std::vector<uuids::uuid>{ capsule->getUUID() });
}

TEST(CapsuleSceneQuery, ASphereBeyondTheSegmentEndOrTheShaftDoesNotOverlap)
{
  const auto scene = fixtures::makeScene();
  addCapsule(scene);

  // 0.9 from the axis would touch an infinite cylinder, but the nearest segment point is 1.14 away.
  EXPECT_TRUE(overlaps(scene, { 0.9f, 1.2f, 0 }, 0.5f).empty());
  EXPECT_TRUE(overlaps(scene, { 1.2f, 0, 0 }, 0.5f).empty());
  EXPECT_TRUE(overlaps(scene, { 0, -1.6f, 0 }, 0.5f).empty());
}
