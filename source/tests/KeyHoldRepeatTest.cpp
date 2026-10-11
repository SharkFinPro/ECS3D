#include <gtest/gtest.h>

#include <KeyHoldRepeat.h>

#include <chrono>
#include <optional>

namespace {
  using std::chrono::milliseconds;

  constexpr milliseconds initialDelay{400};
  constexpr milliseconds interval{125};
  constexpr int keyZ = 90;
  constexpr int keyY = 89;

  class KeyHoldRepeatTest : public testing::Test
  {
  protected:
    KeyHoldRepeat::Clock::time_point m_now{};
    KeyHoldRepeat m_hold{initialDelay, interval, [this] { return m_now; }};

    void advance(const milliseconds amount)
    {
      m_now += amount;
    }
  };
}

TEST_F(KeyHoldRepeatTest, IsInactiveUntilStarted)
{
  EXPECT_FALSE(m_hold.active());
  EXPECT_FALSE(m_hold.poll().has_value());

  m_hold.start(keyZ, EditorAction::undo);
  EXPECT_TRUE(m_hold.active());
}

TEST_F(KeyHoldRepeatTest, DefaultConstructorUsesTheNamedTimings)
{
  KeyHoldRepeat hold{keyHoldInitialDelay, keyHoldRepeatInterval, [this] { return m_now; }};
  hold.start(keyZ, EditorAction::undo);

  advance(keyHoldInitialDelay - milliseconds(1));
  EXPECT_FALSE(hold.poll().has_value());

  advance(milliseconds(1));
  EXPECT_TRUE(hold.poll().has_value());

  advance(keyHoldRepeatInterval - milliseconds(1));
  EXPECT_FALSE(hold.poll().has_value());

  advance(milliseconds(1));
  EXPECT_TRUE(hold.poll().has_value());

  KeyHoldRepeat defaults;
  EXPECT_FALSE(defaults.active());
  defaults.start(keyZ, EditorAction::undo);
  EXPECT_TRUE(defaults.active());
}

TEST_F(KeyHoldRepeatTest, FirstRepeatIsDueExactlyAtTheInitialDelay)
{
  m_hold.start(keyZ, EditorAction::undo);

  advance(initialDelay - milliseconds(1));
  EXPECT_FALSE(m_hold.poll().has_value());

  advance(milliseconds(1));
  EXPECT_EQ(m_hold.poll(), EditorAction::undo);
}

TEST_F(KeyHoldRepeatTest, RepeatsEveryIntervalAfterTheFirst)
{
  m_hold.start(keyZ, EditorAction::undo);
  advance(initialDelay);
  ASSERT_TRUE(m_hold.poll().has_value());

  for (int i = 0; i < 4; ++i)
  {
    advance(interval - milliseconds(1));
    EXPECT_FALSE(m_hold.poll().has_value());

    advance(milliseconds(1));
    EXPECT_EQ(m_hold.poll(), EditorAction::undo);
  }
}

TEST_F(KeyHoldRepeatTest, PollReturnsTheStartedAction)
{
  m_hold.start(keyZ, EditorAction::redo);
  advance(initialDelay);
  EXPECT_EQ(m_hold.poll(), EditorAction::redo);

  m_hold.start(keyZ, EditorAction::undo);
  advance(initialDelay);
  EXPECT_EQ(m_hold.poll(), EditorAction::undo);
}

TEST_F(KeyHoldRepeatTest, ReleasingADifferentKeyKeepsRepeating)
{
  m_hold.start(keyZ, EditorAction::undo);
  m_hold.release(keyY);

  EXPECT_TRUE(m_hold.active());
  advance(initialDelay);
  EXPECT_EQ(m_hold.poll(), EditorAction::undo);
}

TEST_F(KeyHoldRepeatTest, ReleasingTheHeldKeyStopsIt)
{
  m_hold.start(keyZ, EditorAction::undo);
  advance(initialDelay);
  ASSERT_TRUE(m_hold.poll().has_value());

  m_hold.release(keyZ);

  EXPECT_FALSE(m_hold.active());
  advance(initialDelay * 10);
  EXPECT_FALSE(m_hold.poll().has_value());
}

TEST_F(KeyHoldRepeatTest, StopStopsIt)
{
  m_hold.start(keyZ, EditorAction::undo);
  advance(initialDelay);
  ASSERT_TRUE(m_hold.poll().has_value());

  m_hold.stop();

  EXPECT_FALSE(m_hold.active());
  advance(initialDelay * 10);
  EXPECT_FALSE(m_hold.poll().has_value());
}

TEST_F(KeyHoldRepeatTest, StartingAgainRestartsTimingAndReplacesTheAction)
{
  m_hold.start(keyZ, EditorAction::undo);
  advance(initialDelay);
  ASSERT_EQ(m_hold.poll(), EditorAction::undo);

  m_hold.start(keyY, EditorAction::redo);

  advance(initialDelay - milliseconds(1));
  EXPECT_FALSE(m_hold.poll().has_value());

  advance(milliseconds(1));
  EXPECT_EQ(m_hold.poll(), EditorAction::redo);

  m_hold.release(keyZ);
  EXPECT_TRUE(m_hold.active());
}

TEST_F(KeyHoldRepeatTest, ALongGapYieldsOneStepNotABurst)
{
  m_hold.start(keyZ, EditorAction::undo);
  advance(milliseconds(2000));

  EXPECT_EQ(m_hold.poll(), EditorAction::undo);
  EXPECT_FALSE(m_hold.poll().has_value());

  advance(interval - milliseconds(1));
  EXPECT_FALSE(m_hold.poll().has_value());

  advance(milliseconds(1));
  EXPECT_EQ(m_hold.poll(), EditorAction::undo);
}
