#include <gtest/gtest.h>

#include "ConvexMeshFixtures.h"
#include "components/ConvexHullRebuild.h"
#include "objects/components/collisions/ConvexHull.h"
#include "objects/components/collisions/ConvexMeshCollider.h"

#include <glm/vec3.hpp>
#include <vector>

namespace {
  using convexFixtures::sourceModel;
  using convexFixtures::tetrahedron;

  std::vector<glm::vec3> cubePoints(const float half)
  {
    std::vector<glm::vec3> points;
    for (const float x : { -half, half })
    {
      for (const float y : { -half, half })
      {
        for (const float z : { -half, half })
        {
          points.emplace_back(x, y, z);
        }
      }
    }

    return points;
  }

  const std::vector<glm::vec3> coplanarPoints{ { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 2, 2, 0 } };
}

TEST(ConvexHullRebuild, CubePointsBuildAValidHullAndSetTheSource)
{
  ConvexMeshCollider collider;
  EXPECT_TRUE(collider.getSourceModel().is_nil());

  EXPECT_TRUE(rebuildHullFromPoints(collider, cubePoints(2.0f), sourceModel));

  EXPECT_TRUE(isValidConvexHull(collider.getHull()));
  EXPECT_EQ(collider.getHull().vertices.size(), 8u);
  EXPECT_EQ(collider.getSourceModel(), sourceModel);
}

TEST(ConvexHullRebuild, PointsWithNoVolumeLeaveTheColliderUnchanged)
{
  ConvexMeshCollider collider;
  ASSERT_TRUE(collider.setHull(tetrahedron()));
  const ConvexHull before = collider.getHull();

  EXPECT_FALSE(rebuildHullFromPoints(collider, coplanarPoints, sourceModel));

  EXPECT_EQ(collider.getHull().vertices, before.vertices);
  EXPECT_EQ(collider.getHull().indices, before.indices);
  EXPECT_TRUE(collider.getSourceModel().is_nil());

  // Positive control: the same collider does change for points that have volume.
  EXPECT_TRUE(rebuildHullFromPoints(collider, cubePoints(1.0f), sourceModel));
  EXPECT_NE(collider.getHull().vertices, before.vertices);
}

TEST(ConvexHullRebuild, AnIdenticalRebuildReportsNoChange)
{
  ConvexMeshCollider collider;
  ASSERT_TRUE(rebuildHullFromPoints(collider, cubePoints(2.0f), sourceModel));

  EXPECT_FALSE(rebuildHullFromPoints(collider, cubePoints(2.0f), sourceModel));

  // Positive controls: a different source or different points still count as a change.
  const uuids::uuid other = uuids::uuid::from_string("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee").value();
  EXPECT_TRUE(rebuildHullFromPoints(collider, cubePoints(2.0f), other));
  EXPECT_EQ(collider.getSourceModel(), other);
  EXPECT_TRUE(rebuildHullFromPoints(collider, cubePoints(3.0f), other));
}

TEST(ConvexHullRebuild, ReportUnchangedStillReportsAnIdenticalRebuildButNeverAFailure)
{
  ConvexMeshCollider collider;
  ASSERT_TRUE(rebuildHullFromPoints(collider, cubePoints(2.0f), sourceModel));

  EXPECT_FALSE(rebuildHullFromPoints(collider, cubePoints(2.0f), sourceModel));
  EXPECT_TRUE(rebuildHullFromPoints(collider, cubePoints(2.0f), sourceModel, true));

  EXPECT_FALSE(rebuildHullFromPoints(collider, coplanarPoints, sourceModel, true));
  EXPECT_EQ(collider.getHull().vertices.size(), 8u);
}
