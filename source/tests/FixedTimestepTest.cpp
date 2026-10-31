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

TEST(FixedTimestep, ACarriedAccumulatorAndTheFrameTogetherReachingAStepTakeIt)
{
  const auto plan = FixedTimestep::advance(0.125f, 0.125f, fixedDt, maxSteps);

  EXPECT_EQ(plan.steps, 1);
  EXPECT_FLOAT_EQ(plan.remainingAccumulator, 0.0f);
}

TEST(FixedTimestep, ACarriedAccumulatorAndTheFrameTogetherJustShortOfAStepTakeNone)
{
  const auto plan = FixedTimestep::advance(0.125f, 0.0625f, fixedDt, maxSteps);

  EXPECT_EQ(plan.steps, 0);
  EXPECT_FLOAT_EQ(plan.remainingAccumulator, 0.1875f);
}

TEST(FixedTimestep, AFrameExactlyOneStepLongTakesExactlyOneStep)
{
  const auto plan = FixedTimestep::advance(0.0f, fixedDt, fixedDt, maxSteps);

  EXPECT_EQ(plan.steps, 1);
  EXPECT_FLOAT_EQ(plan.remainingAccumulator, 0.0f);
}

TEST(FixedTimestep, ACarriedAccumulatorReachingExactlyTheCapTakesTheCapWithNothingLeft)
{
  const auto plan = FixedTimestep::advance(0.25f, 0.5f, fixedDt, maxSteps);

  EXPECT_EQ(plan.steps, maxSteps);
  EXPECT_FLOAT_EQ(plan.remainingAccumulator, 0.0f);
}

TEST(FixedTimestep, ACarriedAccumulatorPushingOneStepPastTheCapDropsThatStep)
{
  const auto plan = FixedTimestep::advance(0.25f, 0.75f, fixedDt, maxSteps);

  EXPECT_EQ(plan.steps, maxSteps);
  EXPECT_FLOAT_EQ(plan.remainingAccumulator, 0.0f);
}

TEST(FixedTimestep, FeedingTheRemainderBackInTakesEveryStepOfTheTimeThatPassed)
{
  // Seven frames of 0.1875 add up to 1.3125, which is five whole steps and 0.0625 over.
  float accumulator = 0.0f;
  int totalSteps = 0;
  for (int frame = 0; frame < 7; ++frame)
  {
    const auto plan = FixedTimestep::advance(accumulator, 0.1875f, fixedDt, maxSteps);

    EXPECT_LE(plan.steps, 1);
    totalSteps += plan.steps;
    accumulator = plan.remainingAccumulator;
  }

  EXPECT_EQ(totalSteps, 5);
  EXPECT_FLOAT_EQ(accumulator, 0.0625f);
}
