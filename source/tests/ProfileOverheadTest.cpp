#include <gtest/gtest.h>

#include "Lane.h"
#include "ProfileScope.h"

#include <chrono>
#include <cstdint>
#include <iostream>

namespace {
  constexpr int scopeCount = 3'000'000;
  constexpr int scopesPerSample = 100'000;

  // Returns nanoseconds per scope enter/exit pair on the real clock.
  double timeScopes(profile::Lane& lane)
  {
    const auto name = profile::internName("Overhead scope");

    lane.beginSample();
    const auto start = profile::Clock::now();

    for (int i = 0; i < scopeCount; ++i)
    {
      if (i % scopesPerSample == 0 && i != 0)
      {
        lane.endSample(true);
        lane.beginSample();
      }

      const profile::Scope scope(name);
    }

    const auto elapsed = profile::Clock::now() - start;
    lane.endSample(true);

    return static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()) / scopeCount;
  }
}

// Run by hand: --gtest_also_run_disabled_tests --gtest_filter=ProfileOverhead.*
TEST(ProfileOverhead, DISABLED_ScopeCost)
{
  profile::Lane lane("overhead", 8);
  lane.bindToThisThread();

  const double enabledNanos = timeScopes(lane);
  EXPECT_GT(lane.history().newest().nodeCalls[1], 0);

  profile::setEnabled(false);
  const double disabledNanos = timeScopes(lane);
  profile::setEnabled(true);

  profile::Lane::unbindThisThread();

  std::cout << "scope cost, enabled: " << enabledNanos << " ns/scope\n";
  std::cout << "scope cost, disabled: " << disabledNanos << " ns/scope\n";
}
