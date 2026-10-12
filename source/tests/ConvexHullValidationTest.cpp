#include <gtest/gtest.h>

#include "objects/components/collisions/ConvexHull.h"
#include "objects/components/collisions/ConvexHullValidation.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace {
  // Every triangle faces away from the cube's center, which is the origin.
  void expectOutwardWinding(const ConvexHull& hull)
  {
    for (size_t i = 0; i < hull.indices.size(); i += 3)
    {
      const auto& a = hull.vertices[hull.indices[i]];
      const auto& b = hull.vertices[hull.indices[i + 1]];
      const auto& c = hull.vertices[hull.indices[i + 2]];

      EXPECT_GT(glm::dot(glm::cross(b - a, c - a), a), 0.0f) << "triangle " << i / 3;
    }
  }
}

TEST(ConvexHullValidation, TheUnitCubeIsAValidHullWithEightCornersAndTwelveTriangles)
{
  const auto cube = makeUnitCubeHull();

  EXPECT_TRUE(isValidConvexHull(cube));
  EXPECT_EQ(cube.vertices.size(), 8u);
  EXPECT_EQ(cube.indices.size(), 36u);

  expectOutwardWinding(cube);
}

TEST(ConvexHullValidation, RefusesEveryStructuralDefectAndTheControlPasses)
{
  EXPECT_TRUE(isValidConvexHull(makeUnitCubeHull()));

  auto fewVertices = makeUnitCubeHull();
  fewVertices.vertices.resize(3);
  fewVertices.indices = { 0, 1, 2 };

  auto tooManyVertices = makeUnitCubeHull();
  tooManyVertices.vertices.resize(maxConvexHullVertices + 1, glm::vec3(1.0f));

  auto noTriangles = makeUnitCubeHull();
  noTriangles.indices.clear();

  auto ragged = makeUnitCubeHull();
  ragged.indices.pop_back();

  auto tooManyIndices = makeUnitCubeHull();
  tooManyIndices.indices.assign(maxConvexHullIndices + 3, 0);

  auto outOfRange = makeUnitCubeHull();
  outOfRange.indices[5] = 8;

  auto notANumber = makeUnitCubeHull();
  notANumber.vertices[2].y = std::numeric_limits<float>::quiet_NaN();

  auto infinite = makeUnitCubeHull();
  infinite.vertices[7].z = std::numeric_limits<float>::infinity();

  for (const auto* broken : { &fewVertices, &tooManyVertices, &noTriangles, &ragged, &tooManyIndices, &outOfRange,
                              &notANumber, &infinite })
  {
    EXPECT_FALSE(isValidConvexHull(*broken));
  }
}

TEST(ConvexHullValidation, AcceptsAHullAtExactlyEachLimit)
{
  ConvexHull atVertexLimit = makeUnitCubeHull();
  atVertexLimit.vertices.resize(maxConvexHullVertices, glm::vec3(0.0f));
  EXPECT_TRUE(isValidConvexHull(atVertexLimit));

  ConvexHull atIndexLimit = makeUnitCubeHull();
  atIndexLimit.vertices.resize(maxConvexHullVertices, glm::vec3(0.0f));
  atIndexLimit.indices.assign(maxConvexHullIndices, 0);
  EXPECT_TRUE(isValidConvexHull(atIndexLimit));

  atIndexLimit.indices.push_back(0);
  atIndexLimit.indices.push_back(0);
  atIndexLimit.indices.push_back(0);
  EXPECT_FALSE(isValidConvexHull(atIndexLimit));
}
