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
