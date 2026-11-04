#include <gtest/gtest.h>

#include <ProfilerChartLayout.h>

#include <cmath>
#include <cstddef>

TEST(ProfilerChartLayout, ColumnsCoverTheWidthWithoutGapOrOverlap)
{
  for (const std::size_t count : {1u, 3u, 7u, 100u, 333u})
  {
    constexpr float width = 1000.0f;

    EXPECT_FLOAT_EQ(profilerChart::columnSpan(0, count, width).left, 0.0f);
    EXPECT_FLOAT_EQ(profilerChart::columnSpan(count - 1, count, width).right, width);

    for (std::size_t i = 1; i < count; ++i)
    {
      EXPECT_FLOAT_EQ(profilerChart::columnSpan(i - 1, count, width).right,
                      profilerChart::columnSpan(i, count, width).left);
    }
  }
}

TEST(ProfilerChartLayout, ColumnsRoundToWholePixelsAndStayNearTheAverageWidth)
{
  constexpr std::size_t count = 7;
  constexpr float width = 100.0f;

  for (std::size_t i = 0; i < count; ++i)
  {
    const auto span = profilerChart::columnSpan(i, count, width);

    EXPECT_FLOAT_EQ(span.left, std::round(span.left));
    EXPECT_FLOAT_EQ(span.right, std::round(span.right));
    EXPECT_NEAR(span.right - span.left, width / count, 1.0f);
    EXPECT_GT(span.right, span.left);
  }
}

TEST(ProfilerChartLayout, AnEmptyOrOutOfRangeColumnHasNoWidth)
{
  EXPECT_FLOAT_EQ(profilerChart::columnSpan(0, 0, 100.0f).right, 0.0f);
  EXPECT_FLOAT_EQ(profilerChart::columnSpan(5, 5, 100.0f).right, 0.0f);

  EXPECT_GT(profilerChart::columnSpan(4, 5, 100.0f).right, 0.0f);
}

TEST(ProfilerChartLayout, BarHeightIsProportionalToTheScale)
{
  EXPECT_NEAR(profilerChart::barHeight(8350.0, 16.7, 100.0f), 50.0f, 1e-3f);
  EXPECT_NEAR(profilerChart::barHeight(16700.0, 16.7, 100.0f), 100.0f, 1e-3f);
  EXPECT_NEAR(profilerChart::barHeight(8350.0, 33.4, 100.0f), 25.0f, 1e-3f);
}

TEST(ProfilerChartLayout, BarHeightClipsAtTheChartHeight)
{
  EXPECT_FLOAT_EQ(profilerChart::barHeight(50000.0, 16.7, 100.0f), 100.0f);
}

TEST(ProfilerChartLayout, BarHeightIsZeroForNoTimeOrNoScale)
{
  EXPECT_FLOAT_EQ(profilerChart::barHeight(0.0, 16.7, 100.0f), 0.0f);
  EXPECT_FLOAT_EQ(profilerChart::barHeight(5000.0, 0.0, 100.0f), 0.0f);
}

TEST(ProfilerChartLayout, OnlyATotalAboveTheScaleIsMarkedClipped)
{
  EXPECT_TRUE(profilerChart::exceedsScale(16701.0, 16.7));
  EXPECT_FALSE(profilerChart::exceedsScale(16700.0, 16.7));
  EXPECT_FALSE(profilerChart::exceedsScale(1.0e6, 0.0));
}
