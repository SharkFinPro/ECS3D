#include <gtest/gtest.h>

#include "PlayControls.h"
#include <scenes/SceneManager.h>

TEST(PlayControlsTest, StoppedAllowsOnlyPlay)
{
  const auto a = playControlsAvailability(SceneStatus::stopped, true);
  EXPECT_TRUE(a.play);
  EXPECT_FALSE(a.pause);
  EXPECT_FALSE(a.stop);
}

TEST(PlayControlsTest, RunningAllowsPauseAndStop)
{
  const auto a = playControlsAvailability(SceneStatus::running, true);
  EXPECT_FALSE(a.play);
  EXPECT_TRUE(a.pause);
  EXPECT_TRUE(a.stop);
}

TEST(PlayControlsTest, PausedAllowsResumeAndStop)
{
  const auto a = playControlsAvailability(SceneStatus::paused, true);
  EXPECT_TRUE(a.play);
  EXPECT_FALSE(a.pause);
  EXPECT_TRUE(a.stop);
}

TEST(PlayControlsTest, ReadOnlyServerDisablesEverythingWhileStopped)
{
  EXPECT_TRUE(playControlsAvailability(SceneStatus::stopped, true).play);
  const auto a = playControlsAvailability(SceneStatus::stopped, false);
  EXPECT_FALSE(a.play);
  EXPECT_FALSE(a.pause);
  EXPECT_FALSE(a.stop);
}

TEST(PlayControlsTest, ReadOnlyServerDisablesEverythingWhileRunning)
{
  EXPECT_TRUE(playControlsAvailability(SceneStatus::running, true).pause);
  const auto a = playControlsAvailability(SceneStatus::running, false);
  EXPECT_FALSE(a.play);
  EXPECT_FALSE(a.pause);
  EXPECT_FALSE(a.stop);
}

TEST(PlayControlsTest, ReadOnlyServerDisablesEverythingWhilePaused)
{
  EXPECT_TRUE(playControlsAvailability(SceneStatus::paused, true).stop);
  const auto a = playControlsAvailability(SceneStatus::paused, false);
  EXPECT_FALSE(a.play);
  EXPECT_FALSE(a.pause);
  EXPECT_FALSE(a.stop);
}

TEST(PlayControlsTest, PlayLabelReadsResumeOnlyWhilePaused)
{
  EXPECT_STREQ(playControlLabel(SceneStatus::paused), "Resume");
  EXPECT_STREQ(playControlLabel(SceneStatus::stopped), "Play");
  EXPECT_STREQ(playControlLabel(SceneStatus::running), "Play");
}
