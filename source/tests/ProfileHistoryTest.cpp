#include <gtest/gtest.h>

#include "History.h"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {
  profile::Sample sampleWithSequence(const uint64_t sequence)
  {
    profile::Sample sample;
    sample.sequence = sequence;
    return sample;
  }
}

TEST(ProfileHistory, PushingPastCapacityKeepsTheNewestInOrder)
{
  profile::History history(3);

  for (uint64_t sequence = 1; sequence <= 5; ++sequence)
  {
    history.push(sampleWithSequence(sequence));
  }

  ASSERT_EQ(history.size(), 3u);
  EXPECT_EQ(history.capacity(), 3u);
  EXPECT_EQ(history.at(0).sequence, 3u);
  EXPECT_EQ(history.at(1).sequence, 4u);
  EXPECT_EQ(history.at(2).sequence, 5u);
  EXPECT_EQ(history.newest().sequence, 5u);
}

TEST(ProfileHistory, FindSequenceFindsLiveSamplesAndNotOverwrittenOnes)
{
  profile::History history(2);

  for (uint64_t sequence = 1; sequence <= 3; ++sequence)
  {
    history.push(sampleWithSequence(sequence));
  }

  const auto* live = history.findSequence(3);
  ASSERT_NE(live, nullptr);
  EXPECT_EQ(live->sequence, 3u);
  EXPECT_NE(history.findSequence(2), nullptr);
  EXPECT_EQ(history.findSequence(1), nullptr);
  EXPECT_EQ(history.findSequence(99), nullptr);
}

TEST(ProfileHistory, BadAccessThrows)
{
  profile::History history(2);

  EXPECT_THROW(static_cast<void>(history.newest()), std::out_of_range);
  EXPECT_THROW(static_cast<void>(history.at(0)), std::out_of_range);

  history.push(sampleWithSequence(1));

  EXPECT_NO_THROW(static_cast<void>(history.at(0)));
  EXPECT_THROW(static_cast<void>(history.at(1)), std::out_of_range);
  EXPECT_THROW(profile::History(0), std::invalid_argument);
}

TEST(ProfileDurationRing, WrapsAndListsValuesOldestToNewest)
{
  profile::DurationRing ring(3);

  for (uint32_t micros = 1; micros <= 5; ++micros)
  {
    ring.push(micros * 10);
  }

  ASSERT_EQ(ring.size(), 3u);
  EXPECT_EQ(ring.capacity(), 3u);
  EXPECT_EQ(ring.at(0), 30u);
  EXPECT_EQ(ring.at(2), 50u);
  EXPECT_EQ(ring.values(), (std::vector<uint32_t>{30, 40, 50}));
}

TEST(ProfileDurationRing, TotalPushedKeepsCountingPastCapacity)
{
  profile::DurationRing ring(3);
  EXPECT_EQ(ring.totalPushed(), 0u);

  for (uint32_t micros = 0; micros < 10; ++micros)
  {
    ring.push(micros);
  }

  EXPECT_EQ(ring.totalPushed(), 10u);
  EXPECT_EQ(ring.size(), 3u);
}

TEST(ProfileDurationRing, APartlyFilledRingListsOnlyWhatWasPushed)
{
  profile::DurationRing ring(4);
  EXPECT_TRUE(ring.values().empty());

  ring.push(7);
  ring.push(9);

  EXPECT_EQ(ring.size(), 2u);
  EXPECT_EQ(ring.values(), (std::vector<uint32_t>{7, 9}));
}

TEST(ProfileDurationRing, BadCapacityOrAccessThrows)
{
  EXPECT_THROW(profile::DurationRing(0), std::invalid_argument);

  profile::DurationRing ring(2);
  ring.push(1);

  EXPECT_EQ(ring.at(0), 1u);
  EXPECT_THROW(static_cast<void>(ring.at(1)), std::out_of_range);
}
