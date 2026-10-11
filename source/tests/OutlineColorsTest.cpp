#include <gtest/gtest.h>
#include <glm/vec4.hpp>
#include <algorithm>
#include <cmath>
#include <OutlineColors.h>

namespace
{
  void expectOpaqueInRange(const glm::vec4& color)
  {
    EXPECT_FLOAT_EQ(color.a, 1.0f);
    for (int i = 0; i < 4; ++i)
    {
      EXPECT_GE(color[i], 0.0f);
      EXPECT_LE(color[i], 1.0f);
    }
  }
}

TEST(OutlineColors, AreOpaqueAndInRange)
{
  expectOpaqueInRange(selectionOutlineColor);
  expectOpaqueInRange(colliderOutlineColor);
}

TEST(OutlineColors, ColliderReadsApartFromSelection)
{
  float maxDifference = 0.0f;
  for (int i = 0; i < 3; ++i)
  {
    maxDifference = std::max(maxDifference, std::abs(selectionOutlineColor[i] - colliderOutlineColor[i]));
  }

  EXPECT_GT(maxDifference, 0.25f);
}
