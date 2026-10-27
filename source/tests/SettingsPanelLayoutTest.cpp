#include <SettingsPanelLayout.h>
#include <gtest/gtest.h>
#include <array>

TEST(SettingsPanelLayoutTest, EmptyRowHasNoWidth)
{
  EXPECT_EQ(buttonRowWidth({}, 4.0f, 8.0f), 0.0f);
}

TEST(SettingsPanelLayoutTest, OneButtonIsLabelPlusBothPaddings)
{
  const std::array<float, 1> labels = { 40.0f };

  EXPECT_FLOAT_EQ(buttonRowWidth(labels, 4.0f, 8.0f), 48.0f);
}

TEST(SettingsPanelLayoutTest, ThreeButtonsHaveTwoGaps)
{
  const std::array<float, 3> labels = { 40.0f, 32.0f, 24.0f };

  EXPECT_FLOAT_EQ(buttonRowWidth(labels, 4.0f, 8.0f), (40.0f + 32.0f + 24.0f) + 3 * 8.0f + 2 * 8.0f);
}

TEST(SettingsPanelLayoutTest, RowWidthScalesWithEveryInput)
{
  const std::array<float, 3> labels = { 40.0f, 32.0f, 24.0f };
  const std::array<float, 3> scaledLabels = { 50.0f, 40.0f, 30.0f };

  EXPECT_NEAR(buttonRowWidth(scaledLabels, 5.0f, 10.0f), 1.25f * buttonRowWidth(labels, 4.0f, 8.0f), 1e-3f);
}

TEST(SettingsPanelLayoutTest, ThreeTypicalButtonsAtOneTwentyFivePercentExceedTheOldFixedWidth)
{
  constexpr float scale = 1.25f;
  // A 16 px monospace font, the size the editor uses at 125% display scale.
  constexpr float charWidth = 9.6f;
  const std::array<float, 3> labels = { 6 * charWidth, 6 * charWidth, 5 * charWidth };

  EXPECT_GT(buttonRowWidth(labels, 4.0f * scale, 8.0f * scale), 210.0f);
}

TEST(SettingsPanelLayoutTest, DefaultSizeGrowsWithFontSize)
{
  const auto small = settingsWindowDefaultSize(0.0f, 13.0f, 0.0f, 0.0f);
  const auto large = settingsWindowDefaultSize(0.0f, 16.0f, 0.0f, 0.0f);

  EXPECT_GT(large.x, small.x);
  EXPECT_GT(large.y, small.y);
}

TEST(SettingsPanelLayoutTest, DefaultSizeIsNeverNarrowerThanContent)
{
  const auto size = settingsWindowDefaultSize(2000.0f, 13.0f, 4000.0f, 3000.0f);

  EXPECT_FLOAT_EQ(size.x, 2000.0f);
}

TEST(SettingsPanelLayoutTest, DefaultSizeIsCappedToTheViewport)
{
  const auto size = settingsWindowDefaultSize(2000.0f, 13.0f, 1000.0f, 400.0f);

  EXPECT_NEAR(size.x, 900.0f, 1e-3f);
  EXPECT_NEAR(size.y, 360.0f, 1e-3f);
}

TEST(SettingsPanelLayoutTest, ZeroViewportDoesNotCapOrCollapse)
{
  const auto uncapped = settingsWindowDefaultSize(0.0f, 13.0f, 0.0f, 0.0f);
  const auto capped = settingsWindowDefaultSize(0.0f, 13.0f, 300.0f, 200.0f);

  EXPECT_GT(uncapped.x, 0.0f);
  EXPECT_GT(uncapped.y, 0.0f);
  EXPECT_NEAR(capped.x, 270.0f, 1e-3f);
  EXPECT_NEAR(capped.y, 180.0f, 1e-3f);
  EXPECT_GT(uncapped.x, capped.x);
}

TEST(SettingsPanelLayoutTest, NegativeViewportAxisDoesNotCap)
{
  const auto size = settingsWindowDefaultSize(0.0f, 13.0f, -1.0f, -1.0f);

  EXPECT_GT(size.x, 0.0f);
  EXPECT_GT(size.y, 0.0f);
}
