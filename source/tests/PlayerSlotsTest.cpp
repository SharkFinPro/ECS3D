#include <gtest/gtest.h>

#include "PlayerSlots.h"

#include <cstdint>
#include <limits>
#include <optional>

TEST(PlayerSlots, FirstConnectionsGetConsecutiveSlotsFromZero)
{
  PlayerSlots slots;

  EXPECT_EQ(slots.assign(10), 0);
  EXPECT_EQ(slots.assign(11), 1);
}

TEST(PlayerSlots, AssignIsIdempotentAndConsumesNoSlot)
{
  PlayerSlots slots;

  EXPECT_EQ(slots.assign(10), 0);
  EXPECT_EQ(slots.assign(10), 0);
  EXPECT_EQ(slots.assign(11), 1);
  EXPECT_EQ(slots.assign(10), 0);
}

TEST(PlayerSlots, ReleaseReturnsTheFreedSlotAndTheLowestFreeSlotIsReused)
{
  PlayerSlots slots;
  slots.assign(10);
  slots.assign(11);
  slots.assign(12);

  EXPECT_EQ(slots.release(10), std::optional<int32_t>(0));
  EXPECT_EQ(slots.assign(13), 0);

  EXPECT_EQ(slots.assign(11), 1);
  EXPECT_EQ(slots.assign(12), 2);
  EXPECT_EQ(slots.assign(14), 3);
}

TEST(PlayerSlots, ReleaseOfUnknownConnectionChangesNothing)
{
  PlayerSlots slots;
  slots.assign(10);

  EXPECT_EQ(slots.release(99), std::nullopt);

  EXPECT_EQ(slots.assign(10), 0);
  EXPECT_EQ(slots.assign(11), 1);
}

TEST(PlayerSlots, ReleasingTwiceReturnsNothingTheSecondTime)
{
  PlayerSlots slots;
  slots.assign(10);

  EXPECT_EQ(slots.release(10), std::optional<int32_t>(0));
  EXPECT_EQ(slots.release(10), std::nullopt);
}

TEST(PlayerSlots, ReassigningAReleasedConnectionTakesTheLowestFreeSlot)
{
  PlayerSlots slots;
  slots.assign(10);
  slots.assign(11);
  slots.assign(12);

  slots.release(10);
  slots.release(11);

  EXPECT_EQ(slots.assign(11), 0);
  EXPECT_EQ(slots.assign(10), 1);
  EXPECT_EQ(slots.assign(12), 2);
}

TEST(PlayerSlots, ConnectionIdsAreOnlyKeys)
{
  PlayerSlots slots;
  constexpr int32_t big = std::numeric_limits<int32_t>::max();
  constexpr int32_t negative = std::numeric_limits<int32_t>::min();

  EXPECT_EQ(slots.assign(big), 0);
  EXPECT_EQ(slots.assign(negative), 1);
  EXPECT_EQ(slots.assign(-1), 2);

  EXPECT_EQ(slots.release(negative), std::optional<int32_t>(1));
  EXPECT_EQ(slots.assign(5), 1);
  EXPECT_EQ(slots.assign(big), 0);
}
