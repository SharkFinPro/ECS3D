#include <gtest/gtest.h>

#include "GridVisibility.h"

TEST(GridVisibilityTest, ShownWhenEnabledAndFreeFly)
{
  EXPECT_TRUE(shouldShowGrid(true, false));
}

TEST(GridVisibilityTest, HiddenThroughASceneCamera)
{
  EXPECT_TRUE(shouldShowGrid(true, false));
  EXPECT_FALSE(shouldShowGrid(true, true));
}

TEST(GridVisibilityTest, HiddenWhenTheUserTurnsItOff)
{
  EXPECT_TRUE(shouldShowGrid(true, false));
  EXPECT_FALSE(shouldShowGrid(false, false));
}

TEST(GridVisibilityTest, HiddenWhenBothApply)
{
  EXPECT_FALSE(shouldShowGrid(false, true));
}
