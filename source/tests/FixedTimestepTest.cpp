#include <gtest/gtest.h>

#include "FixedTimestep.h"

namespace {
  constexpr float fixedDt = 0.25f;
  constexpr int maxSteps = 3;
}

TEST(FixedTimestep, UnderTheCapKeepsTheRemainder)
{
  const auto plan = FixedTimestep::advance(0.0f, 0.625f, fixedDt, maxSteps);

  EXPECT_EQ(plan.steps, 2);
  EXPECT_FLOAT_EQ(plan.remainingAccumulator, 0.125f);
}

TEST(FixedTimestep, ExactlyTheCapWithASubStepRemainderKeepsTheRemainder)
{
  const auto plan = FixedTimestep::advance(0.125f, 0.75f, fixedDt, maxSteps);

  EXPECT_EQ(plan.steps, maxSteps);
  EXPECT_FLOAT_EQ(plan.remainingAccumulator, 0.125f);
}

TEST(FixedTimestep, ExactlyTheCapWithNothingLeftOverReportsZero)
{
  const auto plan = FixedTimestep::advance(0.0f, 0.75f, fixedDt, maxSteps);

  EXPECT_EQ(plan.steps, maxSteps);
  EXPECT_FLOAT_EQ(plan.remainingAccumulator, 0.0f);
}

TEST(FixedTimestep, MoreThanTheCapDropsTheExcess)
{
  const auto plan = FixedTimestep::advance(0.0f, 5.125f, fixedDt, maxSteps);

  EXPECT_EQ(plan.steps, maxSteps);
  EXPECT_FLOAT_EQ(plan.remainingAccumulator, 0.0f);
}

TEST(FixedTimestep, OneWholeStepLeftAfterTheCapIsDropped)
{
  const auto plan = FixedTimestep::advance(0.0f, 1.0f, fixedDt, maxSteps);

  EXPECT_EQ(plan.steps, maxSteps);
  EXPECT_FLOAT_EQ(plan.remainingAccumulator, 0.0f);
}

TEST(FixedTimestep, JustUnderOneStepLeftAfterTheCapIsKept)
{
  const auto plan = FixedTimestep::advance(0.0f, 0.75f + 0.125f, fixedDt, maxSteps);

  EXPECT_EQ(plan.steps, maxSteps);
  EXPECT_FLOAT_EQ(plan.remainingAccumulator, 0.125f);
}

TEST(FixedTimestep, ZeroDtTakesNoStepsAndKeepsTheAccumulator)
{
  const auto plan = FixedTimestep::advance(0.125f, 0.0f, fixedDt, maxSteps);

  EXPECT_EQ(plan.steps, 0);
  EXPECT_FLOAT_EQ(plan.remainingAccumulator, 0.125f);
}

TEST(FixedTimestep, ZeroMaxStepsTakesNoStepsAndDropsAFullStepBacklog)
{
  const auto plan = FixedTimestep::advance(0.0f, 1.0f, fixedDt, 0);

  EXPECT_EQ(plan.steps, 0);
  EXPECT_FLOAT_EQ(plan.remainingAccumulator, 0.0f);
}
