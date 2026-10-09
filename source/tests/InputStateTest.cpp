#include <gtest/gtest.h>

#include "InputState.h"

#include <cstdint>
#include <initializer_list>

namespace {
  constexpr int32_t slotA = 41;
  constexpr int32_t slotB = 42;
  constexpr int32_t unknownSlot = 43;
  constexpr int keyW = 87;
  constexpr int keyS = 83;

  class InputStateTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
      removeAll();
    }

    void TearDown() override
    {
      removeAll();
    }

  private:
    static void removeAll()
    {
      // InputState is process-wide static, so a slot left behind would leak into the next test.
      InputState::removeSlot(slotA);
      InputState::removeSlot(slotB);
      InputState::removeSlot(unknownSlot);
    }
  };

  TEST_F(InputStateTest, KeyEdgesAreVisibleForOneTickAfterAPressAndARelease)
  {
    InputState::setKeysPressed(slotA, { keyW });
    EXPECT_TRUE(InputState::wasKeyPressedThisTick(slotA, keyW));
    EXPECT_FALSE(InputState::wasKeyReleasedThisTick(slotA, keyW));
    EXPECT_FALSE(InputState::wasKeyPressedThisTick(slotA, keyS));

    InputState::setKeysPressed(slotB, { keyS });
    EXPECT_TRUE(InputState::wasKeyPressedThisTick(slotB, keyS));

    InputState::commitInputEdges();
    EXPECT_FALSE(InputState::wasKeyPressedThisTick(slotB, keyS));
    EXPECT_TRUE(InputState::isKeyPressed(slotB, keyS));
    EXPECT_TRUE(InputState::isKeyPressed(slotA, keyW));
    EXPECT_FALSE(InputState::wasKeyPressedThisTick(slotA, keyW));
    EXPECT_FALSE(InputState::wasKeyReleasedThisTick(slotA, keyW));

    InputState::setKeysPressed(slotA, {});
    EXPECT_TRUE(InputState::wasKeyReleasedThisTick(slotA, keyW));
    EXPECT_FALSE(InputState::wasKeyPressedThisTick(slotA, keyW));

    InputState::commitInputEdges();
    EXPECT_FALSE(InputState::wasKeyReleasedThisTick(slotA, keyW));
    EXPECT_FALSE(InputState::wasKeyPressedThisTick(slotA, keyW));
  }

  TEST_F(InputStateTest, APressAndReleaseBetweenTwoCommitsShowsNoEdge)
  {
    InputState::setKeysPressed(slotA, { keyW });
    InputState::setKeysPressed(slotA, {});

    EXPECT_FALSE(InputState::wasKeyPressedThisTick(slotA, keyW));
    EXPECT_FALSE(InputState::wasKeyReleasedThisTick(slotA, keyW));

    // Positive control: the same press held to the tick boundary does show its edge.
    InputState::setKeysPressed(slotA, { keyW });
    EXPECT_TRUE(InputState::wasKeyPressedThisTick(slotA, keyW));
  }

  TEST_F(InputStateTest, MouseAccumulatesDeltaAndScrollButKeepsTheLatestPositionAndButtons)
  {
    InputState::setMouse(slotA, 10.0f, 20.0f, 1.5f, -2.0f, 0.5f, 0b001u);
    InputState::setMouse(slotA, 30.0f, 40.0f, 2.0f, 4.0f, 0.25f, 0b100u);

    float x = 0.0f;
    float y = 0.0f;
    InputState::getMousePosition(slotA, x, y);
    EXPECT_FLOAT_EQ(x, 30.0f);
    EXPECT_FLOAT_EQ(y, 40.0f);

    InputState::getMouseDelta(slotA, x, y);
    EXPECT_FLOAT_EQ(x, 3.5f);
    EXPECT_FLOAT_EQ(y, 2.0f);
    EXPECT_FLOAT_EQ(InputState::getScroll(slotA), 0.75f);

    EXPECT_FALSE(InputState::isMouseButtonPressed(slotA, 0));
    EXPECT_TRUE(InputState::isMouseButtonPressed(slotA, 2));
  }

  TEST_F(InputStateTest, ClearMouseDeltasZeroesDeltaAndScrollOnly)
  {
    InputState::setMouse(slotA, 10.0f, 20.0f, 1.5f, -2.0f, 0.5f, 0b010u);
    InputState::setMouse(slotB, 5.0f, 6.0f, 3.0f, 4.0f, 1.0f, 0b001u);

    InputState::clearMouseDeltas();

    float x = -1.0f;
    float y = -1.0f;
    for (const auto slot : { slotA, slotB })
    {
      InputState::getMouseDelta(slot, x, y);
      EXPECT_FLOAT_EQ(x, 0.0f);
      EXPECT_FLOAT_EQ(y, 0.0f);
      EXPECT_FLOAT_EQ(InputState::getScroll(slot), 0.0f);
    }

    InputState::getMousePosition(slotA, x, y);
    EXPECT_FLOAT_EQ(x, 10.0f);
    EXPECT_FLOAT_EQ(y, 20.0f);
    EXPECT_TRUE(InputState::isMouseButtonPressed(slotA, 1));
    EXPECT_TRUE(InputState::isMouseButtonPressed(slotB, 0));

    // Deltas accumulate again after a clear.
    InputState::setMouse(slotA, 10.0f, 20.0f, 2.0f, 0.0f, 0.0f, 0b010u);
    InputState::getMouseDelta(slotA, x, y);
    EXPECT_FLOAT_EQ(x, 2.0f);
  }

  TEST_F(InputStateTest, MouseButtonsReadFromTheBitmaskAndOutOfRangeIndicesAreFalse)
  {
    InputState::setMouse(slotA, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0b101u);

    EXPECT_TRUE(InputState::isMouseButtonPressed(slotA, 0));
    EXPECT_FALSE(InputState::isMouseButtonPressed(slotA, 1));
    EXPECT_TRUE(InputState::isMouseButtonPressed(slotA, 2));

    InputState::setMouse(slotA, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0xFFu);
    EXPECT_TRUE(InputState::isMouseButtonPressed(slotA, 7));
    EXPECT_FALSE(InputState::isMouseButtonPressed(slotA, -1));
    EXPECT_FALSE(InputState::isMouseButtonPressed(slotA, 8));  // bit 8 does not exist in a uint8_t either way
  }

  TEST_F(InputStateTest, AnUnknownSlotReadsNeutralNotAnotherSlotsState)
  {
    InputState::setKeysPressed(slotA, { keyW });
    InputState::setFocused(slotA, true);
    InputState::setMouse(slotA, 10.0f, 20.0f, 1.5f, -2.0f, 0.5f, 0b001u);

    float x = 9.0f;
    float y = 9.0f;

    EXPECT_FALSE(InputState::isKeyPressed(unknownSlot, keyW));
    EXPECT_FALSE(InputState::wasKeyPressedThisTick(unknownSlot, keyW));
    EXPECT_FALSE(InputState::wasKeyReleasedThisTick(unknownSlot, keyW));
    EXPECT_FALSE(InputState::isFocused(unknownSlot));
    EXPECT_FALSE(InputState::isMouseButtonPressed(unknownSlot, 0));
    EXPECT_FLOAT_EQ(InputState::getScroll(unknownSlot), 0.0f);
    InputState::getMousePosition(unknownSlot, x, y);
    EXPECT_FLOAT_EQ(x, 0.0f);
    EXPECT_FLOAT_EQ(y, 0.0f);
    x = 9.0f;
    y = 9.0f;
    InputState::getMouseDelta(unknownSlot, x, y);
    EXPECT_FLOAT_EQ(x, 0.0f);
    EXPECT_FLOAT_EQ(y, 0.0f);

    // Positive control: the written slot does read its state back.
    EXPECT_TRUE(InputState::isKeyPressed(slotA, keyW));
    EXPECT_TRUE(InputState::isFocused(slotA));
    EXPECT_TRUE(InputState::isMouseButtonPressed(slotA, 0));
    EXPECT_FLOAT_EQ(InputState::getScroll(slotA), 0.5f);
    EXPECT_TRUE(InputState::isKeyPressed(slotA, keyW));
  }

  TEST_F(InputStateTest, RemoveSlotClearsOnlyThatSlot)
  {
    InputState::setKeysPressed(slotA, { keyW });
    InputState::setFocused(slotA, true);
    InputState::setMouse(slotA, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 0b001u);
    InputState::setKeysPressed(slotB, { keyW });
    InputState::setFocused(slotB, true);

    InputState::removeSlot(slotA);

    EXPECT_FALSE(InputState::isKeyPressed(slotA, keyW));
    EXPECT_FALSE(InputState::isFocused(slotA));
    EXPECT_FALSE(InputState::isMouseButtonPressed(slotA, 0));
    EXPECT_FLOAT_EQ(InputState::getScroll(slotA), 0.0f);
    float x = 9.0f;
    float y = 9.0f;
    InputState::getMousePosition(slotA, x, y);
    EXPECT_FLOAT_EQ(x, 0.0f);
    EXPECT_FLOAT_EQ(y, 0.0f);
    EXPECT_TRUE(InputState::isKeyPressed(slotB, keyW));
    EXPECT_TRUE(InputState::isFocused(slotB));
  }

  TEST_F(InputStateTest, AggregatesReadAcrossEverySlot)
  {
    EXPECT_FALSE(InputState::isAnyKeyPressed(keyW));
    EXPECT_FALSE(InputState::isAnyFocused());

    InputState::setKeysPressed(slotA, {});
    InputState::setKeysPressed(slotB, { keyW });
    InputState::setFocused(slotA, false);
    InputState::setFocused(slotB, true);

    EXPECT_TRUE(InputState::isAnyKeyPressed(keyW));
    EXPECT_FALSE(InputState::isAnyKeyPressed(keyS));
    EXPECT_TRUE(InputState::isAnyFocused());

    InputState::setKeysPressed(slotB, {});
    InputState::setFocused(slotB, false);
    EXPECT_FALSE(InputState::isAnyKeyPressed(keyW));
    EXPECT_FALSE(InputState::isAnyFocused());
  }
}
