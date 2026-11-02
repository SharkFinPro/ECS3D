#include <gtest/gtest.h>

#include "Lane.h"
#include "ProfileScope.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using profile::NameId;
using profile::Scope;

namespace {
  class LaneRig {
  public:
    explicit LaneRig(const std::size_t historyCapacity = 8)
      : lane("test", historyCapacity, [this] { return now; })
    {
      lane.bindToThisThread();
    }
    ~LaneRig() { profile::Lane::unbindThisThread(); }
    void advance(const profile::Clock::duration duration) { now += duration; }
    [[nodiscard]] const profile::Sample& newest() const { return lane.history().newest(); }

    profile::Clock::time_point now = profile::Clock::time_point{} + 1s;
    profile::Lane lane;
  };

  class EnabledGuard {
  public:
    explicit EnabledGuard(const bool enabled) { profile::setEnabled(enabled); }
    ~EnabledGuard() { profile::setEnabled(true); }
  };

  NameId id(const std::string& name) { return profile::internName(name); }

  std::vector<NameId> ids(const std::string& prefix, const std::size_t count)
  {
    std::vector<NameId> result;

    for (std::size_t i = 0; i < count; ++i)
    {
      result.push_back(id(prefix + std::to_string(i)));
    }

    return result;
  }

  void nest(LaneRig& rig, const std::vector<NameId>& names, const std::size_t level)
  {
    if (level == names.size())
    {
      rig.advance(7us);
      return;
    }

    const Scope scope(names[level]);
    nest(rig, names, level + 1);
  }

  void macroScoped(LaneRig& rig)
  {
    ECS3D_PROFILE_SCOPE("test.macro");
    rig.advance(30us);
  }
}

TEST(ProfileLane, NestedScopesBuildTheTreeAndAccumulate)
{
  LaneRig rig;
  const auto outer = id("test.outer");
  const auto inner = id("test.inner");

  rig.lane.beginSample();
  {
    const Scope a(outer);
    rig.advance(100us);
    {
      const Scope b(inner);
      rig.advance(50us);
    }
    rig.advance(10us);
  }
  rig.lane.endSample(true);

  const auto& tree = rig.lane.tree();
  ASSERT_EQ(tree.size(), 3u);
  EXPECT_EQ(tree.name(1), outer);
  EXPECT_EQ(tree.parent(1), 0);
  EXPECT_EQ(tree.name(2), inner);
  EXPECT_EQ(tree.parent(2), 1);
  EXPECT_EQ(rig.newest().nodeMicros[1], 160u);
  EXPECT_EQ(rig.newest().nodeMicros[2], 50u);
  EXPECT_EQ(rig.newest().nodeCalls[1], 1);
  EXPECT_EQ(rig.newest().nodeCalls[2], 1);
}

TEST(ProfileLane, TheSameNameUnderTwoParentsIsTwoNodesAndARepeatedCallSiteAccumulates)
{
  LaneRig rig;
  const auto shared = id("test.shared");

  rig.lane.beginSample();

  for (const auto* parent : {"test.parentA", "test.parentB"})
  {
    const Scope outer(id(parent));

    for (int i = 0; i < 2; ++i)
    {
      const Scope child(shared);
      rig.advance(10us);
    }
  }

  rig.lane.endSample(true);

  const auto& tree = rig.lane.tree();
  ASSERT_EQ(tree.size(), 5u);
  EXPECT_EQ(tree.name(2), shared);
  EXPECT_EQ(tree.name(4), shared);
  EXPECT_NE(tree.parent(2), tree.parent(4));
  EXPECT_EQ(rig.newest().nodeCalls[2], 2);
  EXPECT_EQ(rig.newest().nodeMicros[4], 20u);
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
  EXPECT_EQ(id("test.macro"), rig.lane.tree().name(1));
  EXPECT_EQ(rig.newest().nodeMicros[1], 30u);
#else
  GTEST_SKIP() << "scopes are compiled out";
#endif
}

TEST(ProfileLane, ScopesPastTheDepthCapFoldIntoTheDeepestRecordedScope)
{
  LaneRig atCap;
  atCap.lane.beginSample();
  nest(atCap, ids("test.atCap", profile::maxDepth), 0);
  EXPECT_EQ(atCap.lane.overflowCount(), 0u);

  LaneRig past;
  past.lane.beginSample();
  nest(past, ids("test.depth", profile::maxDepth + 2), 0);
  past.lane.endSample(true);

  EXPECT_EQ(past.lane.overflowCount(), 2u);
  ASSERT_EQ(past.lane.tree().size(), profile::maxDepth + 1);
  EXPECT_EQ(past.newest().nodeMicros[profile::maxDepth], 7u);
  EXPECT_EQ(past.newest().nodeCalls[profile::maxDepth], 1);
}

TEST(ProfileLane, ScopesPastTheNodeCapFoldAndNestedOnesDoNotAttachElsewhere)
{
  LaneRig rig;
  const auto names = ids("test.cap", profile::maxNodes - 1);

  rig.lane.beginSample();

  for (const auto name : names)
  {
    const Scope scope(name);
  }

  { const Scope existing(names[0]); }

  ASSERT_EQ(rig.lane.tree().size(), profile::maxNodes);
  EXPECT_EQ(rig.lane.overflowCount(), 0u);

  {
    const Scope extra(id("test.cap.extra"));
    const Scope nestedExisting(names[0]);
    rig.advance(5us);
  }

  rig.lane.endSample(true);

  EXPECT_EQ(rig.lane.tree().size(), profile::maxNodes);
  EXPECT_EQ(rig.lane.overflowCount(), 2u);
  EXPECT_EQ(rig.newest().nodeCalls[1], 2);
}

TEST(ProfileLane, AScopeOnAnotherThreadOrWhileDisabledRecordsNothing)
{
  LaneRig rig;
  const auto name = id("test.skipped");

  rig.lane.beginSample();
  std::thread([name] { const Scope scope(name); }).join();

  {
    const EnabledGuard guard(false);
    EXPECT_FALSE(profile::isEnabled());
    const Scope scope(name);
  }

  EXPECT_TRUE(profile::isEnabled());
  EXPECT_EQ(rig.lane.tree().size(), 1u);

  { const Scope scope(name); }

  EXPECT_EQ(rig.lane.tree().size(), 2u);
}

TEST(ProfileLane, ASampleKeepsItsTimingAndStaysOpenUntilClosed)
{
  LaneRig rig;
  const auto name = id("test.sample");

  { const Scope closed(name); }

  EXPECT_EQ(rig.lane.tree().size(), 1u);

  rig.advance(500us);
  rig.lane.beginSample();
  {
    const Scope scope(name);
    rig.advance(100us);
  }
  rig.lane.endSample(false);

  EXPECT_TRUE(rig.lane.sampleOpen());
  EXPECT_EQ(rig.lane.history().size(), 0u);

  {
    const Scope scope(name);
    rig.advance(50us);
  }
  rig.lane.endSample(true);

  EXPECT_FALSE(rig.lane.sampleOpen());
  ASSERT_EQ(rig.lane.history().size(), 1u);
  EXPECT_EQ(rig.newest().sequence, 1u);
  EXPECT_EQ(rig.newest().startNanos, 500'000);
  EXPECT_EQ(rig.newest().wallMicros, 150u);
  EXPECT_EQ(rig.newest().nodeMicros[1], 150u);
  EXPECT_EQ(rig.newest().nodeCalls[1], 2);

  rig.lane.beginSample();
  rig.lane.endSample(true);

  EXPECT_EQ(rig.newest().sequence, 2u);
  EXPECT_EQ(rig.newest().startNanos, 650'000);
}

TEST(ProfileLane, AScopeOpenWhenTheSampleClosesIsAbandonedAndItsLaterExitIsStale)
{
  LaneRig rig;
  const auto stale = id("test.stale");
  const auto fresh = id("test.fresh");

  rig.lane.beginSample();
  auto leaked = std::make_unique<Scope>(stale);
  rig.lane.endSample(true);

  EXPECT_EQ(rig.lane.abandonedScopeCount(), 1u);
  EXPECT_EQ(rig.newest().nodeCalls[1], 0);

  rig.lane.beginSample();
  {
    const Scope open(fresh);
    leaked.reset();
    rig.advance(25us);
  }
  rig.lane.endSample(true);

  EXPECT_EQ(rig.lane.abandonedScopeCount(), 1u);
  EXPECT_EQ(rig.lane.tree().parent(2), 0);
  EXPECT_EQ(rig.newest().nodeCalls[1], 0);
  EXPECT_EQ(rig.newest().nodeCalls[2], 1);
  EXPECT_EQ(rig.newest().nodeMicros[2], 25u);
}

TEST(ProfileLane, MicrosAndCallsSaturateInsteadOfWrapping)
{
  LaneRig rig;
  const auto name = id("test.saturate");

  rig.lane.beginSample();
  {
    const Scope scope(name);
    rig.advance(5000s);
  }

  for (int i = 0; i < 70000; ++i)
  {
    const Scope scope(name);
  }

  rig.lane.endSample(true);

  EXPECT_EQ(rig.newest().wallMicros, std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(rig.newest().nodeMicros[1], std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(rig.newest().nodeCalls[1], std::numeric_limits<uint16_t>::max());
}

TEST(ProfileLane, BindingAnotherLaneReplacesTheFirst)
{
  const auto name = id("test.binding");
  LaneRig first;
  first.lane.beginSample();

  {
    profile::Lane second("second", 4);
    second.bindToThisThread();
    second.beginSample();
    { const Scope scope(name); }

    EXPECT_EQ(second.tree().size(), 2u);
    EXPECT_EQ(first.lane.tree().size(), 1u);
  }
}

// A second lane is built at the destroyed lane's address, so a pointer left bound would reach it.
TEST(ProfileLane, DestroyingTheBoundLaneUnbindsIt)
{
  alignas(profile::Lane) std::byte storage[sizeof(profile::Lane)];

  auto* dead = new (storage) profile::Lane("dead", 4);
  dead->bindToThisThread();
  dead->~Lane();

  auto* successor = new (storage) profile::Lane("successor", 4);
  successor->beginSample();
  { const Scope scope(id("test.unbinds")); }

  EXPECT_EQ(successor->tree().size(), 1u);

  successor->bindToThisThread();

  { const Scope scope(id("test.unbinds")); }

  EXPECT_EQ(successor->tree().size(), 2u);
  successor->~Lane();
}

TEST(ProfileLane, DestroyingALaneThatIsNotBoundLeavesTheBindingAlone)
{
  LaneRig bound;
  bound.lane.beginSample();

  {
    const profile::Lane other("other", 4);
  }

  { const Scope scope(id("test.unbound")); }

  EXPECT_EQ(bound.lane.tree().size(), 2u);
}

TEST(ProfileLane, CountersInternSumWithinASampleAndResetInTheNext)
{
  LaneRig rig;
  const auto bodies = rig.lane.counterId("bodies");
  const auto contacts = rig.lane.counterId("contacts");

  EXPECT_NE(bodies, contacts);
  EXPECT_EQ(rig.lane.counterId("bodies"), bodies);

  rig.lane.beginSample();
  rig.lane.addCounter(bodies, 5);
  rig.lane.addCounter(bodies, 7);
  rig.lane.addCounter(contacts, -3);
  rig.lane.endSample(true);

  EXPECT_EQ(rig.newest().counters[bodies], 12);
  EXPECT_EQ(rig.newest().counters[contacts], -3);

  rig.lane.beginSample();
  rig.lane.addCounter(bodies, 1);
  rig.lane.endSample(true);

  EXPECT_EQ(rig.newest().counters[bodies], 1);
  EXPECT_EQ(rig.newest().counters[contacts], 0);
}

TEST(ProfileLane, ACounterAddedWithNoOpenSampleOrAnUnknownIdIsIgnored)
{
  LaneRig rig;
  const auto counter = rig.lane.counterId("ignored");

  rig.lane.addCounter(counter, 100);
  rig.lane.beginSample();
  rig.lane.addCounter(counter, 1);
  rig.lane.addCounter(static_cast<uint8_t>(counter + 1), 50);
  rig.lane.endSample(true);

  EXPECT_EQ(rig.newest().counters[counter], 1);
  EXPECT_EQ(rig.newest().counters[counter + 1], 0);
}

TEST(ProfileLane, MoreCountersThanTheCapThrow)
{
  LaneRig rig;

  for (std::size_t i = 0; i < profile::maxCounters; ++i)
  {
    EXPECT_EQ(rig.lane.counterId("counter" + std::to_string(i)), i);
  }

  EXPECT_THROW(static_cast<void>(rig.lane.counterId("one.too.many")), std::length_error);
  EXPECT_EQ(rig.lane.counterId("counter0"), 0u);
  EXPECT_EQ(rig.lane.counterId("counter7"), 7u);
}

TEST(ProfileLane, ATrackedScopeRingCollectsEveryNestedInstanceAndWraps)
{
  LaneRig rig;
  const auto tracked = id("test.tracked");
  const auto outer = id("test.trackedOuter");

  EXPECT_EQ(rig.lane.tracked(), nullptr);
  rig.lane.trackScope(tracked, 3);
  ASSERT_NE(rig.lane.tracked(), nullptr);
  EXPECT_EQ(rig.lane.tracked()->capacity(), 3u);

  rig.lane.beginSample();
  {
    const Scope a(outer);
    const Scope b(outer);
    const Scope c(tracked);
    rig.advance(10us);
  }
  { const Scope top(tracked); rig.advance(20us); }
  {
    const Scope a(outer);
    const Scope b(tracked);
    rig.advance(30us);
  }
  { const Scope top(tracked); rig.advance(40us); }
  rig.lane.endSample(true);

  EXPECT_EQ(rig.lane.tracked()->values(), (std::vector<uint32_t>{20, 30, 40}));
}

TEST(ProfileLane, AFoldedScopeWithTheTrackedNamePushesNothing)
{
  LaneRig rig;
  const auto tracked = id("test.trackedFolded");
  const auto names = ids("test.trackedDepth", profile::maxDepth);

  rig.lane.trackScope(tracked, 4);
  rig.lane.beginSample();

  { const Scope top(tracked); rig.advance(5us); }

  EXPECT_EQ(rig.lane.tracked()->size(), 1u);

  {
    std::vector<std::unique_ptr<Scope>> open;

    for (const auto name : names)
    {
      open.push_back(std::make_unique<Scope>(name));
    }

    { const Scope folded(tracked); rig.advance(9us); }

    while (!open.empty())
    {
      open.pop_back();
    }
  }

  rig.lane.endSample(true);

  EXPECT_EQ(rig.lane.overflowCount(), 1u);
  EXPECT_EQ(rig.lane.tracked()->values(), (std::vector<uint32_t>{5}));
}
