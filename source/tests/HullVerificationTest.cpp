#include <gtest/gtest.h>

#include "objects/components/collisions/HullFace.h"
#include "objects/components/collisions/HullVerification.h"
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace {
  constexpr double eps = 1e-3;

  // Wound so the normal points away from interior.
  hull::HullFace makeFace(const std::vector<glm::dvec3>& points, const uint32_t a, uint32_t b, uint32_t c,
                          const glm::dvec3& interior)
  {
    const auto build = [&](const uint32_t first, const uint32_t second, const uint32_t third)
    {
      hull::HullFace face;
      face.v = { first, second, third };
      face.normal = glm::normalize(glm::cross(points[second] - points[first], points[third] - points[first]));
      face.offset = glm::dot(face.normal, points[first]);
      return face;
    };

    hull::HullFace face = build(a, b, c);
    if (hull::distanceTo(face, interior) > 0.0)
    {
      face = build(a, c, b);
    }

    return face;
  }

  // Faces over base, base + 1, base + 2, base + 3 of points, in the order {0,1,2} {0,1,3} {0,2,3} {1,2,3}.
  void addTetrahedron(std::vector<glm::dvec3>& points, std::vector<hull::HullFace>& faces, const glm::dvec3& shift)
  {
    const auto base = static_cast<uint32_t>(points.size());
    points.push_back(shift + glm::dvec3(0.0, 0.0, 0.0));
    points.push_back(shift + glm::dvec3(1.0, 0.0, 0.0));
    points.push_back(shift + glm::dvec3(0.0, 1.0, 0.0));
    points.push_back(shift + glm::dvec3(0.0, 0.0, 1.0));

    const glm::dvec3 interior = shift + glm::dvec3(0.25);
    faces.push_back(makeFace(points, base, base + 1, base + 2, interior));
    faces.push_back(makeFace(points, base, base + 1, base + 3, interior));
    faces.push_back(makeFace(points, base, base + 2, base + 3, interior));
    faces.push_back(makeFace(points, base + 1, base + 2, base + 3, interior));
  }

  struct Tetrahedron {
    std::vector<glm::dvec3> points;
    std::vector<hull::HullFace> faces;

    Tetrahedron()
    {
      addTetrahedron(points, faces, glm::dvec3(0.0));
    }
  };

  // The slanted face {1,2,3}, whose outward normal points along (1,1,1).
  size_t slantedFace(const std::vector<hull::HullFace>& faces)
  {
    for (size_t i = 0; i < faces.size(); ++i)
    {
      if (faces[i].normal.x > 0.0 && faces[i].normal.y > 0.0 && faces[i].normal.z > 0.0)
      {
        return i;
      }
    }

    return faces.size();
  }

  glm::dvec3 centroid(const std::vector<glm::dvec3>& points, const hull::HullFace& face)
  {
    return (points[face.v[0]] + points[face.v[1]] + points[face.v[2]]) / 3.0;
  }
}

TEST(HullVerification, AClosedConvexTetrahedronIsVerified)
{
  const Tetrahedron tetrahedron;

  EXPECT_TRUE(hull::isVerifiedHull(tetrahedron.faces, 4, tetrahedron.points, eps));
}

TEST(HullVerification, ASurfaceWithAFaceMissingIsRefusedAsOpen)
{
  Tetrahedron tetrahedron;
  tetrahedron.faces.pop_back();

  EXPECT_FALSE(hull::isVerifiedHull(tetrahedron.faces, 3, tetrahedron.points, eps));
}

TEST(HullVerification, AFlippedFaceIsRefusedForRepeatingADirectedEdge)
{
  Tetrahedron tetrahedron;
  std::swap(tetrahedron.faces[0].v[1], tetrahedron.faces[0].v[2]);

  EXPECT_FALSE(hull::isVerifiedHull(tetrahedron.faces, 4, tetrahedron.points, eps));
}

TEST(HullVerification, AnAliveFaceWithoutANormalIsRefused)
{
  Tetrahedron tetrahedron;
  tetrahedron.faces[0].normal = glm::dvec3(0.0);

  EXPECT_FALSE(hull::isVerifiedHull(tetrahedron.faces, 4, tetrahedron.points, eps));
}

TEST(HullVerification, APointJustPastTheToleranceIsRefusedWhereOneJustInsideIsAccepted)
{
  Tetrahedron tetrahedron;
  const size_t slanted = slantedFace(tetrahedron.faces);
  ASSERT_LT(slanted, tetrahedron.faces.size());
  const auto& face = tetrahedron.faces[slanted];
  const glm::dvec3 onFace = centroid(tetrahedron.points, face);

  auto inside = tetrahedron.points;
  inside.push_back(onFace + face.normal * (9.0 * eps));
  EXPECT_TRUE(hull::isVerifiedHull(tetrahedron.faces, 4, inside, eps));

  auto outside = tetrahedron.points;
  outside.push_back(onFace + face.normal * (11.0 * eps));
  EXPECT_FALSE(hull::isVerifiedHull(tetrahedron.faces, 4, outside, eps));
}

TEST(HullVerification, TwoDisjointTetrahedraAreRefusedByTheEulerCount)
{
  Tetrahedron tetrahedron;
  addTetrahedron(tetrahedron.points, tetrahedron.faces, glm::dvec3(10.0, 0.0, 0.0));

  // Each tetrahedron alone is closed and every edge has its reverse; V + F = 16 but E / 2 + 2 = 14.
  EXPECT_FALSE(hull::isVerifiedHull(tetrahedron.faces, 8, tetrahedron.points, eps));
}

TEST(HullVerification, AMissedPointIsHandedToTheFirstFaceItLiesOutside)
{
  Tetrahedron tetrahedron;
  tetrahedron.points.emplace_back(0.1, 0.1, 0.1);
  tetrahedron.points.emplace_back(-1.0, -1.0, 0.1);

  const auto live = hull::assignMissedPoints(tetrahedron.faces, tetrahedron.points, eps);

  // Outside both the y = 0 face (index 1) and the x = 0 face (index 2), inside the z = 0 face.
  EXPECT_EQ(live, (std::vector<size_t>{ 0, 1, 2, 3 }));
  EXPECT_TRUE(tetrahedron.faces[0].outside.empty());
  EXPECT_EQ(tetrahedron.faces[1].outside, std::vector<uint32_t>{ 5 });
  EXPECT_TRUE(tetrahedron.faces[2].outside.empty());
  EXPECT_TRUE(tetrahedron.faces[3].outside.empty());
}

TEST(HullVerification, InsidePointsAndPointsWithinToleranceAreNotHandedBack)
{
  Tetrahedron tetrahedron;
  tetrahedron.points.emplace_back(0.1, 0.1, 0.1);
  tetrahedron.points.emplace_back(0.2, 0.2, -0.5 * eps);

  EXPECT_TRUE(hull::assignMissedPoints(tetrahedron.faces, tetrahedron.points, eps).empty());
  for (const auto& face : tetrahedron.faces)
  {
    EXPECT_TRUE(face.outside.empty());
  }
}

TEST(HullVerification, HullVerticesAreNeverHandedBack)
{
  Tetrahedron tetrahedron;
  tetrahedron.faces[2].offset = -0.01;

  EXPECT_GT(hull::distanceTo(tetrahedron.faces[2], tetrahedron.points[0]), eps);
  EXPECT_TRUE(hull::assignMissedPoints(tetrahedron.faces, tetrahedron.points, eps).empty());
  EXPECT_TRUE(tetrahedron.faces[2].outside.empty());
}
