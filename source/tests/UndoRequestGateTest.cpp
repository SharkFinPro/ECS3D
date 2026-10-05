#include <gtest/gtest.h>

#include <UndoRequestGate.h>

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>

namespace {
  using std::chrono::milliseconds;

  constexpr milliseconds timeout{500};

  class UndoRequestGateTest : public testing::Test
  {
  protected:
    UndoRequestGate::Clock::time_point m_now{};
    UndoRequestGate m_gate{timeout, [this] { return m_now; }};

    void advance(const milliseconds amount)
    {
      m_now += amount;
    }
  };
}

TEST_F(UndoRequestGateTest, IsNotBlockedInitially)
{
  EXPECT_FALSE(m_gate.blocked());
}

TEST_F(UndoRequestGateTest, IsBlockedAfterBeginUntilTheTimeoutElapses)
{
  m_gate.begin();

  EXPECT_TRUE(m_gate.blocked());

  advance(timeout - milliseconds(1));
  EXPECT_TRUE(m_gate.blocked());

  advance(milliseconds(1));
  EXPECT_FALSE(m_gate.blocked());
}

TEST_F(UndoRequestGateTest, ExpiryClearsTheGateSoALaterBeginStartsAFreshWindow)
{
  m_gate.begin();
  advance(timeout * 3);
  ASSERT_FALSE(m_gate.blocked());

  m_gate.begin();
  EXPECT_TRUE(m_gate.blocked());

  advance(timeout - milliseconds(1));
  EXPECT_TRUE(m_gate.blocked());
}

TEST_F(UndoRequestGateTest, ClearUnblocksImmediately)
{
  m_gate.begin();
  ASSERT_TRUE(m_gate.blocked());

  m_gate.clear();

  EXPECT_FALSE(m_gate.blocked());
}

TEST_F(UndoRequestGateTest, ABeginRestartsTheTimeoutWindow)
{
  m_gate.begin();
  advance(timeout - milliseconds(1));

  m_gate.begin();
  advance(timeout - milliseconds(1));

  EXPECT_TRUE(m_gate.blocked());
}

TEST(UndoRequestGate, DefaultsToTheRealClock)
{
  UndoRequestGate gate{std::chrono::hours(1)};

  EXPECT_FALSE(gate.blocked());

  gate.begin();

  EXPECT_TRUE(gate.blocked());
}

TEST(GainedOneEntry, IsTrueOnlyForExactlyOneMore)
{
  EXPECT_TRUE(gainedOneEntry(0, 1));
  EXPECT_TRUE(gainedOneEntry(4, 5));

  EXPECT_FALSE(gainedOneEntry(3, 3));
  EXPECT_FALSE(gainedOneEntry(3, 2));
  EXPECT_FALSE(gainedOneEntry(3, 0));
  EXPECT_FALSE(gainedOneEntry(3, 5));
}

TEST(CanActOnHistoryItem, RequiresALabelAnEditableServerAndNoRequestInFlight)
{
  const std::optional<std::string> label = "Rename Cube";
  const std::optional<std::string> none;

  EXPECT_TRUE(canActOnHistoryItem(label, true, false));

  EXPECT_FALSE(canActOnHistoryItem(none, true, false));
  EXPECT_FALSE(canActOnHistoryItem(label, false, false));
  EXPECT_FALSE(canActOnHistoryItem(label, true, true));
  EXPECT_FALSE(canActOnHistoryItem(none, false, true));
}
