#include <gtest/gtest.h>

#include "HullGizmoMesh.h"
#include <objects/components/collisions/ConvexHullValidation.h>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <limits>

namespace {
  constexpr float tolerance = 1e-5f;
}

TEST(HullGizmoMeshTest, UnitCubeGetsThreeVerticesPerTriangle)
{
  const ConvexHull hull = makeUnitCubeHull();
  const HullGizmoMesh mesh = buildHullGizmoMesh(hull);

  ASSERT_EQ(hull.indices.size(), 36u);
  EXPECT_EQ(mesh.indices.size(), 36u);
  EXPECT_EQ(mesh.positions.size(), 36u);
  EXPECT_EQ(mesh.normals.size(), 36u);
}

TEST(HullGizmoMeshTest, IndicesAreInRangeAndSequential)
{
  const HullGizmoMesh mesh = buildHullGizmoMesh(makeUnitCubeHull());

  for (size_t i = 0; i < mesh.indices.size(); ++i)
  {
    EXPECT_EQ(mesh.indices[i], i);
    EXPECT_LT(mesh.indices[i], mesh.positions.size());
  }
}

TEST(HullGizmoMeshTest, NormalsAreUnitAndPointOutward)
{
  const ConvexHull hull = makeUnitCubeHull();
  const HullGizmoMesh mesh = buildHullGizmoMesh(hull);

  glm::vec3 centroid(0.0f);
  for (const glm::vec3& vertex : hull.vertices)
  {
    centroid += vertex;
  }
  centroid /= static_cast<float>(hull.vertices.size());

  for (size_t i = 0; i < mesh.positions.size(); ++i)
  {
    EXPECT_NEAR(glm::length(mesh.normals[i]), 1.0f, tolerance);
    EXPECT_GT(glm::dot(mesh.normals[i], mesh.positions[i] - centroid), 0.0f);
  }
}

TEST(HullGizmoMeshTest, AllThreeVerticesOfATriangleShareItsNormal)
{
  const HullGizmoMesh mesh = buildHullGizmoMesh(makeUnitCubeHull());

  for (size_t i = 0; i + 2 < mesh.normals.size(); i += 3)
  {
    EXPECT_EQ(mesh.normals[i], mesh.normals[i + 1]);
    EXPECT_EQ(mesh.normals[i], mesh.normals[i + 2]);
  }
}

TEST(HullGizmoMeshTest, PositionsFollowTheHullTriangles)
{
  const ConvexHull hull = makeUnitCubeHull();
  const HullGizmoMesh mesh = buildHullGizmoMesh(hull);

  for (size_t i = 0; i < hull.indices.size(); ++i)
  {
    EXPECT_EQ(mesh.positions[i], hull.vertices[hull.indices[i]]);
  }
}

TEST(HullGizmoMeshTest, DegenerateAndOutOfRangeTrianglesAreSkipped)
{
  ConvexHull hull;
  hull.vertices = { {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {2, 0, 0} };
  // Good triangle, a zero-area one (collinear), one past the vertices.
  hull.indices = { 0, 1, 2, 0, 1, 3, 0, 1, 9 };

  const HullGizmoMesh mesh = buildHullGizmoMesh(hull);

  EXPECT_EQ(mesh.indices.size(), 3u);
  ASSERT_EQ(mesh.normals.size(), 3u);
  EXPECT_NEAR(mesh.normals[0].z, 1.0f, tolerance);
}

TEST(HullGizmoMeshTest, AnIndexAtOrPastTheVertexCountSkipsTheTriangleInAnyPosition)
{
  ConvexHull hull;
  hull.vertices = { {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0} };
  // One good triangle, then the bad index (9 is past the end, 4 equals the vertex count) in each slot.
  hull.indices = { 0, 1, 2, 9, 1, 2, 0, 9, 2, 0, 1, 4, 4, 1, 2, 0, 4, 2 };

  EXPECT_EQ(buildHullGizmoMesh(hull).indices.size(), 3u);
}

TEST(HullGizmoMeshTest, ATriangleWithANonFiniteVertexIsSkipped)
{
  ConvexHull hull;
  hull.vertices = { {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {std::numeric_limits<float>::quiet_NaN(), 0, 0} };
  hull.indices = { 0, 1, 2, 0, 1, 3 };

  const HullGizmoMesh mesh = buildHullGizmoMesh(hull);

  EXPECT_EQ(mesh.indices.size(), 3u);
  EXPECT_EQ(mesh.normals.size(), 3u);
}

TEST(HullGizmoMeshTest, EmptyHullGivesAnEmptyMesh)
{
  const HullGizmoMesh mesh = buildHullGizmoMesh(ConvexHull{});

  EXPECT_TRUE(mesh.positions.empty());
  EXPECT_TRUE(mesh.indices.empty());
}

TEST(HullGizmoMeshTest, TrailingIndicesThatDoNotFormATriangleAreIgnored)
{
  ConvexHull hull;
  hull.vertices = { {0, 0, 0}, {1, 0, 0}, {0, 1, 0} };
  hull.indices = { 0, 1, 2, 0, 1 };

  EXPECT_EQ(buildHullGizmoMesh(hull).indices.size(), 3u);
}
