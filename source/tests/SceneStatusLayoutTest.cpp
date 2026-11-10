#include <gtest/gtest.h>

#include "SceneStatusLayout.h"

TEST(SceneStatusLayoutTest, EverythingFitsOnOneRow)
{
  ToolbarFlow flow(1600.0f);

  EXPECT_FALSE(flow.place(90.0f, 18.0f));
  EXPECT_TRUE(flow.place(130.0f, 18.0f));
  EXPECT_TRUE(flow.place(260.0f, 18.0f));
  EXPECT_TRUE(flow.place(230.0f, 18.0f));
  EXPECT_EQ(flow.rows(), 1);
}

TEST(SceneStatusLayoutTest, FirstGroupAlwaysStartsTheFirstRow)
{
  ToolbarFlow flow(10.0f);

  EXPECT_FALSE(flow.place(500.0f, 18.0f));
  EXPECT_EQ(flow.rows(), 1);
}

TEST(SceneStatusLayoutTest, ExactFitContinuesTheRow)
{
  ToolbarFlow flow(100.0f);

  EXPECT_FALSE(flow.place(60.0f, 0.0f));
  EXPECT_TRUE(flow.place(22.0f, 18.0f));
  EXPECT_EQ(flow.rows(), 1);
}

TEST(SceneStatusLayoutTest, OnePixelOverWraps)
{
  ToolbarFlow flow(100.0f);

  EXPECT_FALSE(flow.place(60.0f, 0.0f));
  EXPECT_FALSE(flow.place(23.0f, 18.0f));
  EXPECT_EQ(flow.rows(), 2);
}

TEST(SceneStatusLayoutTest, TheGapCountsTowardTheFit)
{
  ToolbarFlow fitsWithoutGap(100.0f);
  EXPECT_FALSE(fitsWithoutGap.place(60.0f, 0.0f));
  EXPECT_TRUE(fitsWithoutGap.place(40.0f, 0.0f));

  ToolbarFlow withGap(100.0f);
  EXPECT_FALSE(withGap.place(60.0f, 0.0f));
  EXPECT_FALSE(withGap.place(40.0f, 18.0f));
  EXPECT_EQ(withGap.rows(), 2);
}

TEST(SceneStatusLayoutTest, AWrappedRowResetsTheUsedWidth)
{
  ToolbarFlow flow(100.0f);

  EXPECT_FALSE(flow.place(80.0f, 18.0f));
  EXPECT_FALSE(flow.place(50.0f, 18.0f));
  EXPECT_TRUE(flow.place(20.0f, 18.0f));
  EXPECT_FALSE(flow.place(20.0f, 18.0f));
  EXPECT_EQ(flow.rows(), 3);
}

TEST(SceneStatusLayoutTest, AnOversizedGroupSitsAloneAndTheNextWraps)
{
  ToolbarFlow flow(100.0f);

  EXPECT_FALSE(flow.place(30.0f, 18.0f));
  EXPECT_FALSE(flow.place(250.0f, 18.0f));
  EXPECT_FALSE(flow.place(10.0f, 18.0f));
  EXPECT_EQ(flow.rows(), 3);
}

TEST(SceneStatusLayoutTest, ZeroWidthsStayOnOneRow)
{
  ToolbarFlow flow(1600.0f);

  EXPECT_FALSE(flow.place(0.0f, 18.0f));
  for (int i = 0; i < 6; ++i)
  {
    EXPECT_TRUE(flow.place(0.0f, 18.0f));
  }
  EXPECT_EQ(flow.rows(), 1);
}

TEST(SceneStatusLayoutTest, TheRealToolbarWrapsAtNarrowWidthsOnly)
{
  const float widths[] = {90.0f, 130.0f, 270.0f, 230.0f, 200.0f, 130.0f, 220.0f};

  ToolbarFlow narrow(1000.0f);
  for (const float width : widths)
  {
    [[maybe_unused]] const bool continued = narrow.place(width, 18.0f);
  }
  EXPECT_GT(narrow.rows(), 1);

  ToolbarFlow wide(2200.0f);
  for (const float width : widths)
  {
    [[maybe_unused]] const bool continued = wide.place(width, 18.0f);
  }
  EXPECT_EQ(wide.rows(), 1);
}

TEST(SceneStatusLayoutTest, PanelHeightGrowsByOneRowPerRow)
{
  EXPECT_FLOAT_EQ(toolbarPanelHeight(1, 24.0f, 8.0f, 28.0f), 68.0f);
  EXPECT_FLOAT_EQ(toolbarPanelHeight(3, 24.0f, 8.0f, 28.0f), 124.0f);
}

TEST(SceneStatusLayoutTest, UniformButtonWidthKeepsTheMinimumForShortLabels)
{
  EXPECT_FLOAT_EQ(uniformLabelButtonWidth(30.0f, 11.0f, 60.0f), 60.0f);
}

TEST(SceneStatusLayoutTest, UniformButtonWidthGrowsToFitTheWidestLabel)
{
  EXPECT_FLOAT_EQ(uniformLabelButtonWidth(46.0f, 11.0f, 60.0f), 68.0f);
}
