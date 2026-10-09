#include <gtest/gtest.h>

#include "LightGizmoVisibility.h"
#include <scenes/SceneManager.h>

TEST(LightGizmoVisibilityTest, DrawnWhileStoppedOnTheRasterPath)
{
  EXPECT_TRUE(shouldDrawLightGizmos(SceneStatus::stopped, false));
}

TEST(LightGizmoVisibilityTest, HiddenWhileRunning)
{
  EXPECT_TRUE(shouldDrawLightGizmos(SceneStatus::stopped, false));
  EXPECT_FALSE(shouldDrawLightGizmos(SceneStatus::running, false));
}

TEST(LightGizmoVisibilityTest, HiddenWhilePaused)
{
  EXPECT_TRUE(shouldDrawLightGizmos(SceneStatus::stopped, false));
  EXPECT_FALSE(shouldDrawLightGizmos(SceneStatus::paused, false));
}

TEST(LightGizmoVisibilityTest, HiddenWhileStoppedAndRayTracing)
{
  EXPECT_TRUE(shouldDrawLightGizmos(SceneStatus::stopped, false));
  EXPECT_FALSE(shouldDrawLightGizmos(SceneStatus::stopped, true));
}

TEST(LightGizmoVisibilityTest, HiddenWhileRunningAndRayTracing)
{
  EXPECT_TRUE(shouldDrawLightGizmos(SceneStatus::stopped, false));
  EXPECT_FALSE(shouldDrawLightGizmos(SceneStatus::running, true));
}
