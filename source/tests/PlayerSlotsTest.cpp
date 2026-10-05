#include <gtest/gtest.h>

#include "PlayerSlots.h"

#include <cstdint>
#include <limits>
#include <optional>

TEST(PlayerSlots, FirstConnectionsGetConsecutiveSlotsFromZero)
{
  PlayerSlots slots;

  EXPECT_EQ(slots.assign(10).slot, 0);
  EXPECT_EQ(slots.assign(11).slot, 1);
}

TEST(PlayerSlots, AssignIsIdempotentAndConsumesNoSlot)
{
  PlayerSlots slots;

  EXPECT_EQ(slots.assign(10).slot, 0);
  EXPECT_EQ(slots.assign(10).slot, 0);
  EXPECT_EQ(slots.assign(11).slot, 1);
  EXPECT_EQ(slots.assign(10).slot, 0);
}

TEST(PlayerSlots, AssignReportsCreationOnlyForTheFirstBinding)
{
  PlayerSlots slots;

  const auto first = slots.assign(10);
  EXPECT_TRUE(first.created);
  EXPECT_EQ(first.slot, 0);

  const auto repeat = slots.assign(10);
  EXPECT_FALSE(repeat.created);
  EXPECT_EQ(repeat.slot, 0);

  EXPECT_TRUE(slots.assign(11).created);
}

TEST(PlayerSlots, ReleaseReturnsTheFreedSlotAndTheLowestFreeSlotIsReused)
{
  PlayerSlots slots;
  (void)slots.assign(10);
  (void)slots.assign(11);
  (void)slots.assign(12);

  EXPECT_EQ(slots.release(10), std::optional<int32_t>(0));
  EXPECT_EQ(slots.assign(13).slot, 0);

  EXPECT_EQ(slots.assign(11).slot, 1);
  EXPECT_EQ(slots.assign(12).slot, 2);
  EXPECT_EQ(slots.assign(14).slot, 3);
}

TEST(PlayerSlots, ReleaseOfUnknownConnectionChangesNothing)
{
  PlayerSlots slots;
  (void)slots.assign(10);

  EXPECT_EQ(slots.release(99), std::nullopt);

  EXPECT_EQ(slots.assign(10).slot, 0);
  EXPECT_EQ(slots.assign(11).slot, 1);
}

TEST(PlayerSlots, ReleasingTwiceReturnsNothingTheSecondTime)
{
  PlayerSlots slots;
  (void)slots.assign(10);

  EXPECT_EQ(slots.release(10), std::optional<int32_t>(0));
  EXPECT_EQ(slots.release(10), std::nullopt);
}

TEST(PlayerSlots, ReassigningAReleasedConnectionTakesTheLowestFreeSlot)
{
  PlayerSlots slots;
  (void)slots.assign(10);
  (void)slots.assign(11);
  (void)slots.assign(12);

  slots.release(10);
  slots.release(11);

  EXPECT_EQ(slots.assign(11).slot, 0);
  EXPECT_EQ(slots.assign(10).slot, 1);
  EXPECT_EQ(slots.assign(12).slot, 2);
}

TEST(PlayerSlots, ConnectionIdsAreOnlyKeys)
{
  PlayerSlots slots;
  constexpr int32_t big = std::numeric_limits<int32_t>::max();
  constexpr int32_t negative = std::numeric_limits<int32_t>::min();

  EXPECT_EQ(slots.assign(big).slot, 0);
  EXPECT_EQ(slots.assign(negative).slot, 1);
  EXPECT_EQ(slots.assign(-1).slot, 2);

  EXPECT_EQ(slots.release(negative), std::optional<int32_t>(1));
  EXPECT_EQ(slots.assign(5).slot, 1);
  EXPECT_EQ(slots.assign(big).slot, 0);
}
