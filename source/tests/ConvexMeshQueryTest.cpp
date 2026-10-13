#include <gtest/gtest.h>

#include "TestPrinters.h"
#include "TestScene.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/ConvexHull.h"
#include "objects/components/collisions/ConvexMeshCollider.h"
#include "queries/HullQueries.h"
#include "queries/SceneQueries.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>
#include <uuid.h>

namespace {
  constexpr uint32_t allLayers = 0xFFFFFFFFu;

  using fixtures::makeScene;
  using fixtures::Scene;

  struct QueryHit
  {
    uuids::uuid object;
    glm::vec3 point{ 0.0f };
    glm::vec3 normal{ 0.0f };
    float distance = 0.0f;
  };

  std::shared_ptr<Object> addHull(const Scene& scene, const glm::vec3& position,
                                  const glm::vec3& scale = glm::vec3(1))
  {
    auto object = addObject(scene, "Hull", position, scale);
    object->addComponent(std::make_shared<ConvexMeshCollider>());

    return object;
  }

  std::shared_ptr<Object> addTetrahedron(const Scene& scene, const glm::vec3& position)
  {
    auto object = addObject(scene, "Tetrahedron", position);
    const auto hull = std::make_shared<ConvexMeshCollider>();
    const std::vector<glm::vec3> points{ { -1, -1, -1 }, { 1, -1, -1 }, { 0, -1, 1 }, { 0, 1, 0 } };
    EXPECT_TRUE(hull->setHull(*buildConvexHull(points)));
    object->addComponent(hull);

    return object;
  }

  bool castRay(const Scene& scene, const glm::vec3& origin, const glm::vec3& direction, const float maxDistance,
               QueryHit& hit)
  {
    return SceneQueries::raycast(*scene.objectManager, origin, direction, maxDistance, allLayers, {},
                                 hit.object, hit.point, hit.normal, hit.distance);
  }

  std::vector<uuids::uuid> sphereOverlaps(const Scene& scene, const glm::vec3& center, const float radius)
  {
    std::vector<uuids::uuid> results;
    SceneQueries::overlapSphere(*scene.objectManager, center, radius, allLayers, {}, results);

    return results;
  }

  bool overlapsObject(const Scene& scene, const std::shared_ptr<Object>& object, const glm::vec3& center,
                      const float radius)
  {
    const auto results = sphereOverlaps(scene, center, radius);

    return std::ranges::find(results, object->getUUID()) != results.end();
  }

  void expectNear(const char* what, const glm::vec3& actual, const glm::vec3& expected)
  {
    fixtures::expectNear(what, actual, expected, 1e-4f);
  }
}

TEST(ConvexMeshQuery, ARayHitsTheNearFaceOfAHullAndReportsItsNormal)
{
  const auto scene = makeScene();
  const auto hull = addHull(scene, { 0, 0, 0 });

  QueryHit hit;
  ASSERT_TRUE(castRay(scene, { 5, 0, 0 }, { -1, 0, 0 }, 100.0f, hit));

  EXPECT_EQ(hit.object, hull->getUUID());
  EXPECT_NEAR(hit.distance, 4.0f, 1e-4f);
  expectNear("point", hit.point, { 1, 0, 0 });
  expectNear("normal", hit.normal, { 1, 0, 0 });
}

TEST(ConvexMeshQuery, ARayThatPassesBesideOrFallsShortOfAHullMisses)
{
  const auto scene = makeScene();
  addHull(scene, { 0, 0, 0 });

  QueryHit hit;

  // The control: the same ray aimed at the hull hits.
  EXPECT_TRUE(castRay(scene, { 5, 0.5f, 0 }, { -1, 0, 0 }, 100.0f, hit));

  EXPECT_FALSE(castRay(scene, { 5, 3, 0 }, { -1, 0, 0 }, 100.0f, hit));
  EXPECT_FALSE(castRay(scene, { 5, 0, 0 }, { -1, 0, 0 }, 3.0f, hit));
  EXPECT_FALSE(castRay(scene, { 5, 0, 0 }, { 1, 0, 0 }, 100.0f, hit));
}

TEST(ConvexMeshQuery, TheHullIsPlacedByItsTransform)
{
  const auto scene = makeScene();
  addHull(scene, { 10, 0, 0 }, { 2, 1, 1 });

  QueryHit hit;
  ASSERT_TRUE(castRay(scene, { 20, 0, 0 }, { -1, 0, 0 }, 100.0f, hit));

  // Spans x 8..12 once scaled.
  EXPECT_NEAR(hit.distance, 8.0f, 1e-4f);
  expectNear("normal", hit.normal, { 1, 0, 0 });
}

TEST(ConvexMeshQuery, ARayStartedInsideAHullReportsContactAtItsOrigin)
{
  const auto scene = makeScene();
  addHull(scene, { 0, 0, 0 });

  QueryHit hit;
  ASSERT_TRUE(castRay(scene, { 0.5f, 0.2f, 0.1f }, { 0, 0, 1 }, 100.0f, hit));

  EXPECT_FLOAT_EQ(hit.distance, 0.0f);
  expectNear("normal", hit.normal, { 0, 0, -1 });
}

TEST(ConvexMeshQuery, ARayHitsASlantedFaceWithItsOwnNormal)
{
  const auto scene = makeScene();
  addTetrahedron(scene, { 0, 0, 0 });

  QueryHit hit;
  ASSERT_TRUE(castRay(scene, { 0, 0, -5 }, { 0, 0, 1 }, 100.0f, hit));

  // The back face runs from the base edge (y -1, z -1) up to the apex (y 1, z 0), so its plane is
  // y - 2z = 1: at y = 0 the ray meets it at z = -0.5, and the outward normal leans up and back.
  EXPECT_NEAR(hit.distance, 4.5f, 1e-4f);
  expectNear("point", hit.point, { 0, 0, -0.5f });
  expectNear("normal", hit.normal, { 0, 0.4472136f, -0.8944272f });
}

TEST(ConvexMeshQuery, ASphereInsideOrTouchingAHullOverlapsItAndOneBeyondItDoesNot)
{
  const auto scene = makeScene();
  const auto hull = addHull(scene, { 0, 0, 0 });

  EXPECT_TRUE(overlapsObject(scene, hull, { 0.5f, 0.5f, 0.5f }, 0.1f));
  EXPECT_TRUE(overlapsObject(scene, hull, { 1.5f, 0, 0 }, 0.6f));
  EXPECT_FALSE(overlapsObject(scene, hull, { 1.5f, 0, 0 }, 0.4f));
  EXPECT_FALSE(overlapsObject(scene, hull, { 0, 5, 0 }, 1.0f));
}

TEST(ConvexMeshQuery, ASphereNearACornerOverlapsOnlyWhenItReachesTheCornerItself)
{
  const auto scene = makeScene();
  const auto hull = addHull(scene, { 0, 0, 0 });

  // Each face plane is 0.5 from this center, so a test of the planes alone would call 0.6 an overlap. The
  // corner at (1, 1, 1) is 0.866 away.
  const glm::vec3 center(1.5f, 1.5f, 1.5f);

  EXPECT_FALSE(overlapsObject(scene, hull, center, 0.6f));
  EXPECT_FALSE(overlapsObject(scene, hull, center, 0.8f));
  EXPECT_TRUE(overlapsObject(scene, hull, center, 0.9f));
}

TEST(ConvexMeshQuery, ASphereBesideAnEdgeOverlapsOnlyWhenItReachesTheEdge)
{
  const auto scene = makeScene();
  const auto hull = addHull(scene, { 0, 0, 0 });

  // 0.5 from two face planes, 0.707 from the edge along z.
  const glm::vec3 center(1.5f, 1.5f, 0.0f);

  EXPECT_FALSE(overlapsObject(scene, hull, center, 0.6f));
  EXPECT_TRUE(overlapsObject(scene, hull, center, 0.75f));
}

TEST(HullQueries, TheClosestPointOnATriangleIsTheNearestVertexEdgeOrFacePoint)
{
  const glm::vec3 a(0, 0, 0);
  const glm::vec3 b(2, 0, 0);
  const glm::vec3 c(0, 2, 0);

  expectNear("face", hullQueries::closestPointOnTriangle({ 0.5f, 0.5f, 3 }, a, b, c), { 0.5f, 0.5f, 0 });
  expectNear("vertex", hullQueries::closestPointOnTriangle({ -1, -1, 0 }, a, b, c), a);
  expectNear("edge ab", hullQueries::closestPointOnTriangle({ 1, -2, 0 }, a, b, c), { 1, 0, 0 });
  expectNear("hypotenuse", hullQueries::closestPointOnTriangle({ 2, 2, 0 }, a, b, c), { 1, 1, 0 });
  expectNear("vertex c", hullQueries::closestPointOnTriangle({ 0, 5, 0 }, a, b, c), c);
}

TEST(ConvexMeshQuery, AMirroredHullIsQueriedLikeTheUnmirroredOne)
{
  // One negative scale axis flips the winding of every triangle in world space; the queries must still treat
  // the faces as facing outward.
  for (const float mirror : { 1.0f, -1.0f })
  {
    SCOPED_TRACE(mirror > 0.0f ? "unmirrored" : "mirrored");

    const auto scene = makeScene();
    const auto hull = addHull(scene, { 0, 0, 0 }, { mirror, 1, 1 });

    QueryHit hit;
    ASSERT_TRUE(castRay(scene, { 5, 0, 0 }, { -1, 0, 0 }, 100.0f, hit));
    EXPECT_NEAR(hit.distance, 4.0f, 1e-4f);
    expectNear("normal", hit.normal, { 1, 0, 0 });

    EXPECT_TRUE(overlapsObject(scene, hull, { 0.5f, 0.5f, 0.5f }, 0.1f));
    EXPECT_TRUE(overlapsObject(scene, hull, { 1.5f, 0, 0 }, 0.6f));
    EXPECT_FALSE(overlapsObject(scene, hull, { 1.5f, 0, 0 }, 0.4f));
  }
}

TEST(ConvexMeshQuery, AHullCollapsedByAZeroScaleOverlapsNothing)
{
  const auto scene = makeScene();
  const auto collapsed = addHull(scene, { 0, 0, 0 }, { 0, 0, 0 });
  const auto normal = addHull(scene, { 10, 0, 0 });

  // The control: the unscaled hull overlaps a sphere sitting in it.
  EXPECT_TRUE(overlapsObject(scene, normal, { 10, 0, 0 }, 0.1f));

  EXPECT_FALSE(overlapsObject(scene, collapsed, { 5, 0, 0 }, 0.1f));
  EXPECT_FALSE(overlapsObject(scene, collapsed, { 0, 3, 0 }, 0.5f));

  QueryHit hit;
  ASSERT_TRUE(castRay(scene, { 15, 0, 0 }, { -1, 0, 0 }, 100.0f, hit));
  EXPECT_EQ(hit.object, normal->getUUID());
}

TEST(ConvexMeshQuery, AVerySmallHullBehavesLikeAScaledDownHull)
{
  const auto scene = makeScene();
  const float size = 1e-5f;
  const auto hull = addHull(scene, { 0, 0, 0 }, glm::vec3(size));

  EXPECT_TRUE(overlapsObject(scene, hull, { 0.5f * size, 0, 0 }, 0.1f * size));
  EXPECT_TRUE(overlapsObject(scene, hull, { 1.5f * size, 0, 0 }, 0.6f * size));
  EXPECT_FALSE(overlapsObject(scene, hull, { 1.5f * size, 0, 0 }, 0.4f * size));
  EXPECT_FALSE(overlapsObject(scene, hull, { 1.0f, 0, 0 }, 0.1f));

  QueryHit hit;
  ASSERT_TRUE(castRay(scene, { 1.0f, 0, 0 }, { -1, 0, 0 }, 100.0f, hit));
  EXPECT_NEAR(hit.distance, 1.0f - size, 1e-4f);
}
