#include <gtest/gtest.h>

#include "InputFocus.h"

namespace {

input::InputSnapshot movingSnapshot()
{
  input::InputSnapshot snapshot;
  snapshot.keys = { 87 };
  snapshot.mouseDeltaX = 4.0f;
  snapshot.mouseDeltaY = -3.0f;
  snapshot.scrollY = 2.0f;
  snapshot.buttons = input::mouseButtonLeft | input::mouseButtonRight;
  return snapshot;
}

}

TEST(InputFocus, TheMainWindowAloneCountsAsFocused)
{
  EXPECT_TRUE(input::anyWindowFocused(true, false));
}

TEST(InputFocus, AFocusedDetachedWindowCountsWhenTheMainWindowIsNot)
{
  EXPECT_TRUE(input::anyWindowFocused(false, true));
}

TEST(InputFocus, NoFocusedWindowMeansUnfocused)
{
  EXPECT_FALSE(input::anyWindowFocused(false, false));
}

TEST(InputFocus, FocusKeepsMouseMotionScrollButtonsAndKeys)
{
  auto snapshot = movingSnapshot();
  input::applyFocus(snapshot, input::anyWindowFocused(false, true));

  EXPECT_TRUE(snapshot.focused);
  EXPECT_FLOAT_EQ(snapshot.mouseDeltaX, 4.0f);
  EXPECT_FLOAT_EQ(snapshot.mouseDeltaY, -3.0f);
  EXPECT_FLOAT_EQ(snapshot.scrollY, 2.0f);
  EXPECT_EQ(snapshot.buttons, input::mouseButtonLeft | input::mouseButtonRight);
  EXPECT_EQ(snapshot.keys.size(), 1u);
}

TEST(InputFocus, NoFocusZeroesMouseMotionScrollAndButtonsButKeepsKeys)
{
  auto snapshot = movingSnapshot();
  input::applyFocus(snapshot, input::anyWindowFocused(false, false));

  EXPECT_FALSE(snapshot.focused);
  EXPECT_FLOAT_EQ(snapshot.mouseDeltaX, 0.0f);
  EXPECT_FLOAT_EQ(snapshot.mouseDeltaY, 0.0f);
  EXPECT_FLOAT_EQ(snapshot.scrollY, 0.0f);
  EXPECT_EQ(snapshot.buttons, 0);
  EXPECT_EQ(snapshot.keys.size(), 1u);
}
