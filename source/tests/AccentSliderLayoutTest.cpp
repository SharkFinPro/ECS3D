#include <gtest/gtest.h>

#include <AccentSliderLayout.h>

TEST(AccentSliderLayoutTest, TrackEndsBeforeTheValueColumn)
{
  constexpr float column = 60.0f;

  for (const float width : {120.0f, 200.0f, 333.0f, 800.0f})
  {
    const SliderLayout layout = sliderLayout(10.0f, width, column);

    EXPECT_FLOAT_EQ(layout.trackStart, 10.0f);
    EXPECT_FLOAT_EQ(layout.valueRight, 10.0f + width);
    EXPECT_GT(layout.trackLength(), 0.0f);
    EXPECT_LE(layout.trackEnd + sliderValueGap, layout.valueRight - column);
  }
}

TEST(AccentSliderLayoutTest, RowsWithTheSameRightEdgeShareTheTrackEnd)
{
  constexpr float rightEdge = 400.0f;
  constexpr float column = 60.0f;

  const SliderLayout shortLabel = sliderLayout(80.0f, rightEdge - 80.0f, column);
  const SliderLayout longLabel = sliderLayout(140.0f, rightEdge - 140.0f, column);

  EXPECT_NE(shortLabel.trackStart, longLabel.trackStart);
  EXPECT_FLOAT_EQ(shortLabel.trackEnd, longLabel.trackEnd);
  EXPECT_FLOAT_EQ(shortLabel.valueRight, longLabel.valueRight);
  EXPECT_FLOAT_EQ(shortLabel.trackEnd, rightEdge - column - sliderValueGap);
}

TEST(AccentSliderLayoutTest, MouseMapsAcrossTheTrack)
{
  const SliderLayout layout = sliderLayout(100.0f, 300.0f, 60.0f);

  EXPECT_FLOAT_EQ(sliderValueAt(layout.trackStart, layout, 2.0f, 12.0f), 2.0f);
  EXPECT_FLOAT_EQ(sliderValueAt(layout.trackEnd, layout, 2.0f, 12.0f), 12.0f);
  EXPECT_NEAR(sliderValueAt((layout.trackStart + layout.trackEnd) * 0.5f, layout, 2.0f, 12.0f), 7.0f, 1e-4f);
}

TEST(AccentSliderLayoutTest, MouseOverTheNumberClampsToMax)
{
  const SliderLayout layout = sliderLayout(100.0f, 300.0f, 60.0f);

  EXPECT_FLOAT_EQ(sliderValueAt(layout.valueRight, layout, 0.0f, 1.0f), 1.0f);
  EXPECT_FLOAT_EQ(sliderValueAt(layout.trackEnd + sliderValueGap, layout, 0.0f, 1.0f), 1.0f);
  EXPECT_FLOAT_EQ(sliderValueAt(layout.trackStart - 50.0f, layout, 0.0f, 1.0f), 0.0f);
}

TEST(AccentSliderLayoutTest, NarrowRowGetsAZeroLengthTrack)
{
  const SliderLayout layout = sliderLayout(10.0f, 30.0f, 60.0f);

  EXPECT_FLOAT_EQ(layout.trackLength(), 0.0f);
  EXPECT_FLOAT_EQ(layout.trackEnd, layout.trackStart);
  EXPECT_FLOAT_EQ(sliderValueAt(500.0f, layout, 3.0f, 9.0f), 3.0f);

  const SliderLayout negative = sliderLayout(10.0f, -5.0f, 60.0f);
  EXPECT_FLOAT_EQ(negative.trackLength(), 0.0f);
}
