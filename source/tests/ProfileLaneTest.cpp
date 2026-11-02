#include <gtest/gtest.h>

#include "Lane.h"
#include "ProfileScope.h"

#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {
  struct FakeClock {
    profile::Clock::time_point now = profile::Clock::time_point{} + 1s;

    void advance(const profile::Clock::duration duration)
    {
      now += duration;
    }
  };

  class LaneRig {
  public:
    explicit LaneRig(const std::size_t historyCapacity = 8)
      : lane("test", historyCapacity, [this] { return clock.now; })
    {
      lane.bindToThisThread();
    }

    ~LaneRig()
    {
      profile::Lane::unbindThisThread();
    }

    FakeClock clock;
    profile::Lane lane;
  };

  class EnabledGuard {
  public:
    explicit EnabledGuard(const bool enabled)
    {
      profile::setEnabled(enabled);
    }

    ~EnabledGuard()
    {
      profile::setEnabled(true);
    }
  };

  profile::NameId id(const std::string& name)
  {
    return profile::internName(name);
  }

  void nest(LaneRig& rig, const std::vector<profile::NameId>& ids, const std::size_t level)
  {
    if (level == ids.size())
    {
      rig.clock.advance(7us);
      return;
    }

    const profile::Scope scope(ids[level]);
    nest(rig, ids, level + 1);
  }

  std::vector<profile::NameId> distinctIds(const std::string& prefix, const std::size_t count)
  {
    std::vector<profile::NameId> ids;

    for (std::size_t i = 0; i < count; ++i)
    {
      ids.push_back(id(prefix + std::to_string(i)));
    }

    return ids;
  }

  void macroScoped(LaneRig& rig)
  {
    ECS3D_PROFILE_SCOPE("test.macro");
    rig.clock.advance(30us);
  }
}

TEST(ProfileLane, NestedScopesBuildTheTreeAndAccumulate)
{
  LaneRig rig;
  const auto outer = id("test.outer");
  const auto inner = id("test.inner");

  rig.lane.beginSample();
  {
    const profile::Scope a(outer);
    rig.clock.advance(100us);
    {
      const profile::Scope b(inner);
      rig.clock.advance(50us);
    }
    rig.clock.advance(10us);
  }
  rig.lane.endSample(true);

  const auto& tree = rig.lane.tree();
  ASSERT_EQ(tree.size(), 3u);
  EXPECT_EQ(tree.name(1), outer);
  EXPECT_EQ(tree.parent(1), 0);
  EXPECT_EQ(tree.name(2), inner);
  EXPECT_EQ(tree.parent(2), 1);

  const auto& sample = rig.lane.history().newest();
  EXPECT_EQ(sample.nodeMicros[1], 160u);
  EXPECT_EQ(sample.nodeMicros[2], 50u);
  EXPECT_EQ(sample.nodeCalls[1], 1);
  EXPECT_EQ(sample.nodeCalls[2], 1);
  EXPECT_EQ(sample.wallMicros, 160u);
}

TEST(ProfileLane, TheSameNameUnderTwoParentsIsTwoNodes)
{
  LaneRig rig;
  const auto a = id("test.parentA");
  const auto b = id("test.parentB");
  const auto shared = id("test.shared");

  rig.lane.beginSample();
  {
    const profile::Scope outerA(a);
    const profile::Scope child(shared);
    rig.clock.advance(10us);
  }
  {
    const profile::Scope outerB(b);
    const profile::Scope child(shared);
    rig.clock.advance(20us);
  }
  rig.lane.endSample(true);

  ASSERT_EQ(rig.lane.tree().size(), 5u);
  EXPECT_EQ(rig.lane.tree().name(2), shared);
  EXPECT_EQ(rig.lane.tree().name(4), shared);
  EXPECT_NE(rig.lane.tree().parent(2), rig.lane.tree().parent(4));

  const auto& sample = rig.lane.history().newest();
  EXPECT_EQ(sample.nodeMicros[2], 10u);
  EXPECT_EQ(sample.nodeMicros[4], 20u);
}

TEST(ProfileLane, TheSameCallSiteTwiceInOneSampleAccumulates)
{
  LaneRig rig;
  const auto name = id("test.twice");

  rig.lane.beginSample();

  for (int i = 0; i < 2; ++i)
  {
    const profile::Scope scope(name);
    rig.clock.advance(40us);
  }

  rig.lane.endSample(true);

  EXPECT_EQ(rig.lane.tree().size(), 2u);
  EXPECT_EQ(rig.lane.history().newest().nodeCalls[1], 2);
  EXPECT_EQ(rig.lane.history().newest().nodeMicros[1], 80u);
}

TEST(ProfileLane, TheMacroRecordsIntoTheBoundLane)
{
#if ECS3D_PROFILER_SCOPES
  LaneRig rig;

  rig.lane.beginSample();
  macroScoped(rig);
  rig.lane.endSample(true);

  ASSERT_EQ(rig.lane.tree().size(), 2u);
  EXPECT_EQ(profile::nameOf(rig.lane.tree().name(1)), "test.macro");
  EXPECT_EQ(rig.lane.history().newest().nodeMicros[1], 30u);
#else
  GTEST_SKIP() << "scopes are compiled out";
#endif
}

TEST(ProfileLane, InterningTheSameTextGivesTheSameId)
{
  const auto first = id("test.intern");

  EXPECT_EQ(id("test.intern"), first);
  EXPECT_NE(id("test.intern.other"), first);
  EXPECT_EQ(profile::nameOf(first), "test.intern");
}

TEST(ProfileLane, ScopesPastTheDepthCapFoldIntoTheDeepestRecordedScope)
{
  LaneRig rig;

  rig.lane.beginSample();
  nest(rig, distinctIds("test.depthAtCap", profile::maxDepth), 0);
  EXPECT_EQ(rig.lane.overflowCount(), 0u);
  EXPECT_EQ(rig.lane.tree().size(), profile::maxDepth + 1);
  rig.lane.endSample(true);

  const auto deep = distinctIds("test.depth", profile::maxDepth + 2);
  LaneRig other;

  other.lane.beginSample();
  nest(other, deep, 0);
  other.lane.endSample(true);

  EXPECT_EQ(other.lane.overflowCount(), 2u);
  ASSERT_EQ(other.lane.tree().size(), profile::maxDepth + 1);
  EXPECT_EQ(other.lane.history().newest().nodeMicros[profile::maxDepth], 7u);
  EXPECT_EQ(other.lane.history().newest().nodeCalls[profile::maxDepth], 1);
}

TEST(ProfileLane, ScopesPastTheNodeCapFoldAndNestedOnesDoNotAttachElsewhere)
{
  LaneRig rig;
  const auto ids = distinctIds("test.cap", profile::maxNodes - 1);

  rig.lane.beginSample();

  for (const auto name : ids)
  {
    const profile::Scope scope(name);
  }

  ASSERT_EQ(rig.lane.tree().size(), profile::maxNodes);
  EXPECT_EQ(rig.lane.overflowCount(), 0u);

  {
    const profile::Scope existing(ids[0]);
  }

  EXPECT_EQ(rig.lane.overflowCount(), 0u);

  {
    const profile::Scope extra(id("test.cap.extra"));
    const profile::Scope nestedExisting(ids[0]);
    rig.clock.advance(5us);
  }

  rig.lane.endSample(true);

  EXPECT_EQ(rig.lane.tree().size(), profile::maxNodes);
  EXPECT_EQ(rig.lane.overflowCount(), 2u);
  EXPECT_EQ(rig.lane.history().newest().nodeCalls[1], 2);
}

TEST(ProfileLane, AScopeOnAnotherThreadRecordsNothing)
{
  LaneRig rig;
  const auto name = id("test.thread");

  rig.lane.beginSample();

  std::thread other([name] { const profile::Scope scope(name); });
  other.join();

  EXPECT_EQ(rig.lane.tree().size(), 1u);

  {
    const profile::Scope scope(name);
  }

  EXPECT_EQ(rig.lane.tree().size(), 2u);
}

TEST(ProfileLane, ADisabledProfilerRecordsNothing)
{
  LaneRig rig;
  const auto name = id("test.enabled");

  rig.lane.beginSample();

  {
    const EnabledGuard guard(false);
    EXPECT_FALSE(profile::isEnabled());
    const profile::Scope scope(name);
  }

  EXPECT_TRUE(profile::isEnabled());
  EXPECT_EQ(rig.lane.tree().size(), 1u);

  {
    const profile::Scope scope(name);
  }

  EXPECT_EQ(rig.lane.tree().size(), 2u);
}

TEST(ProfileLane, AScopeWithNoOpenSampleRecordsNothing)
{
  LaneRig rig;
  const auto name = id("test.closed");

  {
    const profile::Scope scope(name);
    rig.clock.advance(10us);
  }

  EXPECT_EQ(rig.lane.tree().size(), 1u);
  EXPECT_FALSE(rig.lane.sampleOpen());

  rig.lane.beginSample();
  {
    const profile::Scope scope(name);
  }

  EXPECT_EQ(rig.lane.tree().size(), 2u);
}

TEST(ProfileLane, EndingWithoutClosingKeepsTheSampleOpen)
{
  LaneRig rig;
  const auto name = id("test.keepOpen");

  rig.lane.beginSample();
  {
    const profile::Scope scope(name);
    rig.clock.advance(100us);
  }
  rig.lane.endSample(false);

  EXPECT_TRUE(rig.lane.sampleOpen());
  EXPECT_EQ(rig.lane.history().size(), 0u);

  {
    const profile::Scope scope(name);
    rig.clock.advance(50us);
  }
  rig.lane.endSample(true);

  EXPECT_FALSE(rig.lane.sampleOpen());
  ASSERT_EQ(rig.lane.history().size(), 1u);
  EXPECT_EQ(rig.lane.history().newest().nodeMicros[1], 150u);
  EXPECT_EQ(rig.lane.history().newest().nodeCalls[1], 2);
}

TEST(ProfileLane, ClosingASamplePushesItsTiming)
{
  LaneRig rig;

  rig.clock.advance(500us);
  rig.lane.beginSample();
  rig.clock.advance(200us);
  rig.lane.endSample(true);
  rig.clock.advance(300us);
  rig.lane.beginSample();
  rig.clock.advance(40us);
  rig.lane.endSample(true);

  ASSERT_EQ(rig.lane.history().size(), 2u);

  const auto& first = rig.lane.history().at(0);
  EXPECT_EQ(first.sequence, 1u);
  EXPECT_EQ(first.startNanos, 500'000);
  EXPECT_EQ(first.wallMicros, 200u);

  const auto& second = rig.lane.history().at(1);
  EXPECT_EQ(second.sequence, 2u);
  EXPECT_EQ(second.startNanos, 1'000'000);
  EXPECT_EQ(second.wallMicros, 40u);
}

TEST(ProfileLane, AScopeOpenWhenTheSampleClosesIsAbandonedAndItsLaterExitIsStale)
{
  LaneRig rig;
  const auto stale = id("test.stale");
  const auto fresh = id("test.fresh");

  rig.lane.beginSample();
  auto leaked = std::make_unique<profile::Scope>(stale);
  rig.clock.advance(10us);
  rig.lane.endSample(true);

  EXPECT_EQ(rig.lane.abandonedScopeCount(), 1u);
  EXPECT_EQ(rig.lane.history().newest().nodeCalls[1], 0);

  rig.lane.beginSample();
  {
    const profile::Scope open(fresh);
    leaked.reset();
    rig.clock.advance(25us);
  }
  rig.lane.endSample(true);

  const auto& sample = rig.lane.history().newest();
  EXPECT_EQ(rig.lane.abandonedScopeCount(), 1u);
  EXPECT_EQ(rig.lane.tree().name(1), stale);
  EXPECT_EQ(sample.nodeCalls[1], 0);
  EXPECT_EQ(rig.lane.tree().name(2), fresh);
  EXPECT_EQ(rig.lane.tree().parent(2), 0);
  EXPECT_EQ(sample.nodeCalls[2], 1);
  EXPECT_EQ(sample.nodeMicros[2], 25u);
}

TEST(ProfileLane, CountersSumWithinASample)
{
  LaneRig rig;
  const auto bodies = rig.lane.counterId("bodies");
  const auto pairs = rig.lane.counterId("pairs");

  EXPECT_NE(bodies, pairs);
  EXPECT_EQ(rig.lane.counterId("bodies"), bodies);

  rig.lane.addCounter(bodies, 5);
  rig.lane.beginSample();
  rig.lane.addCounter(bodies, 3);
  rig.lane.addCounter(bodies, 4);
  rig.lane.addCounter(pairs, -2);
  rig.lane.endSample(true);
  rig.lane.addCounter(bodies, 100);

  rig.lane.beginSample();
  rig.lane.endSample(true);

  EXPECT_EQ(rig.lane.history().at(0).counters[bodies], 7);
  EXPECT_EQ(rig.lane.history().at(0).counters[pairs], -2);
  EXPECT_EQ(rig.lane.history().at(1).counters[bodies], 0);
}

TEST(ProfileLane, CounterIdsThrowPastTheMaximum)
{
  LaneRig rig;

  for (std::size_t i = 0; i < profile::maxCounters; ++i)
  {
    EXPECT_EQ(rig.lane.counterId("counter" + std::to_string(i)), i);
  }

  EXPECT_THROW(static_cast<void>(rig.lane.counterId("one too many")), std::length_error);
  EXPECT_EQ(rig.lane.counterId("counter0"), 0);
}

TEST(ProfileLane, ATrackedScopeRecordsEveryInstanceAtAnyDepthAndWraps)
{
  LaneRig rig;
  const auto outer = id("test.trackOuter");
  const auto tracked = id("test.tracked");

  EXPECT_EQ(rig.lane.tracked(), nullptr);
  rig.lane.trackScope(tracked, 3);
  ASSERT_NE(rig.lane.tracked(), nullptr);

  rig.lane.beginSample();
  {
    const profile::Scope top(tracked);
    rig.clock.advance(1us);
  }
  {
    const profile::Scope wrapper(outer);
    for (const auto micros : {2us, 3us, 4us})
    {
      const profile::Scope nested(tracked);
      rig.clock.advance(micros);
    }
    rig.clock.advance(99us);
  }
  rig.lane.endSample(true);

  EXPECT_EQ(rig.lane.tracked()->values(), (std::vector<uint32_t>{2, 3, 4}));
}

TEST(ProfileLane, MicrosAndCallsSaturateInsteadOfWrapping)
{
  LaneRig rig;
  const auto name = id("test.saturate");

  rig.lane.beginSample();
  {
    const profile::Scope scope(name);
    rig.clock.advance(std::chrono::seconds(5000));
  }

  for (int i = 0; i < 70000; ++i)
  {
    const profile::Scope scope(name);
  }

  rig.lane.endSample(true);

  const auto& sample = rig.lane.history().newest();
  EXPECT_EQ(sample.wallMicros, std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(sample.nodeMicros[1], std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(sample.nodeCalls[1], std::numeric_limits<uint16_t>::max());
}

TEST(ProfileLane, BindingAnotherLaneReplacesTheFirstAndDestroyingUnbinds)
{
  const auto name = id("test.binding");
  FakeClock clock;
  const auto now = [&clock] { return clock.now; };

  profile::Lane first("first", 4, now);
  first.bindToThisThread();
  first.beginSample();

  {
    profile::Lane second("second", 4, now);
    second.bindToThisThread();
    second.beginSample();

    {
      const profile::Scope scope(name);
    }

    EXPECT_EQ(second.tree().size(), 2u);
    EXPECT_EQ(first.tree().size(), 1u);
  }

  {
    const profile::Scope scope(name);
  }

  EXPECT_EQ(first.tree().size(), 1u);

  first.bindToThisThread();

  {
    const profile::Scope scope(name);
  }

  EXPECT_EQ(first.tree().size(), 2u);
  profile::Lane::unbindThisThread();
}
