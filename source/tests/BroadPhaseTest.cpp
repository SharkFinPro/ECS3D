#include <gtest/gtest.h>

#include "broadphase/BroadPhase.h"

#include <glm/vec3.hpp>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace {
  struct Lcg {
    uint32_t state = 99u;

    float next(const float low, const float high)
    {
      state = state * 1664525u + 1013904223u;
      return low + (high - low) * static_cast<float>((state >> 8) & 0xFFFFu) / 65535.0f;
    }
  };

  Aabb boxAt(const glm::vec3& center, const float halfSize = 0.5f)
  {
    return { center - glm::vec3(halfSize), center + glm::vec3(halfSize) };
  }

  std::vector<uint64_t> pairsOf(const BroadPhase& broadPhase)
  {
    const auto pairs = broadPhase.getPairs();
    return { pairs.begin(), pairs.end() };
  }

  uint64_t pairKey(const int32_t a, const int32_t b)
  {
    const auto lower = static_cast<uint64_t>(std::min(a, b));
    const auto higher = static_cast<uint64_t>(std::max(a, b));
    return (lower << 32) | higher;
  }

  bool hasPair(const BroadPhase& broadPhase, const int32_t a, const int32_t b)
  {
    const auto pairs = pairsOf(broadPhase);
    return std::ranges::binary_search(pairs, pairKey(a, b));
  }

  struct Body {
    int32_t proxy = BroadPhase::nullProxy;
    Aabb tight;
    bool dynamic = false;
    bool alive = false;
  };

  // The cache's whole contract, checked against brute force: sorted and unique; every pair has a dynamic side;
  // it is exactly the set of live pairs whose fat boxes overlap; and so it holds every tight overlap.
  void expectCacheExact(const BroadPhase& broadPhase, const std::vector<Body>& bodies, const int tick)
  {
    const auto pairs = pairsOf(broadPhase);
    ASSERT_TRUE(std::ranges::is_sorted(pairs)) << "tick " << tick;
    ASSERT_EQ(std::adjacent_find(pairs.begin(), pairs.end()), pairs.end()) << "duplicate pair, tick " << tick;

    std::vector<uint64_t> expected;
    size_t tightOverlaps = 0;
    for (size_t i = 0; i < bodies.size(); ++i)
    {
      for (size_t j = i + 1; j < bodies.size(); ++j)
      {
        const Body& a = bodies[i];
        const Body& b = bodies[j];
        if (!a.alive || !b.alive || (!a.dynamic && !b.dynamic) || !a.tight.isFinite() || !b.tight.isFinite())
        {
          continue;
        }

        if (broadPhase.getFatBox(a.proxy).overlaps(broadPhase.getFatBox(b.proxy)))
        {
          expected.push_back(pairKey(a.proxy, b.proxy));
        }

        if (a.tight.overlaps(b.tight))
        {
          ++tightOverlaps;
          EXPECT_TRUE(std::ranges::binary_search(pairs, pairKey(a.proxy, b.proxy)))
            << "missed a touching pair, tick " << tick;
        }
      }
    }

    std::ranges::sort(expected);
    EXPECT_EQ(pairs, expected) << "tick " << tick;
    EXPECT_GE(pairs.size(), tightOverlaps);

    for (const auto& body : bodies)
    {
      if (body.alive && body.tight.isFinite())
      {
        EXPECT_TRUE(broadPhase.getFatBox(body.proxy).contains(body.tight)) << "tick " << tick;
      }
    }

    EXPECT_TRUE(broadPhase.getStaticTree().isValid());
    EXPECT_TRUE(broadPhase.getDynamicTree().isValid());
  }
}

TEST(BroadPhase, TwoStaticProxiesNeverPair)
{
  BroadPhase broadPhase;
  const auto first = broadPhase.createProxy(boxAt({ 0, 0, 0 }), false, 0);
  const auto second = broadPhase.createProxy(boxAt({ 0.5f, 0, 0 }), false, 1);
  broadPhase.updatePairs();

  EXPECT_TRUE(broadPhase.getPairs().empty());

  // Positive control: the same overlap with one side dynamic pairs.
  const auto dynamic = broadPhase.createProxy(boxAt({ 0.25f, 0, 0 }), true, 2);
  broadPhase.updatePairs();

  EXPECT_EQ(pairsOf(broadPhase), (std::vector{ pairKey(first, dynamic), pairKey(second, dynamic) }));
}

TEST(BroadPhase, AProxyStayingInsideItsFatBoxIsNotQueriedAgain)
{
  BroadPhase broadPhase;
  const auto proxy = broadPhase.createProxy(boxAt({ 0, 0, 0 }), true, 0);
  broadPhase.updatePairs();
  ASSERT_EQ(broadPhase.getLastMovedCount(), 1u);

  broadPhase.moveProxy(proxy, boxAt({ BroadPhase::margin * 0.5f, 0, 0 }));
  broadPhase.updatePairs();
  EXPECT_EQ(broadPhase.getLastMovedCount(), 0u);

  // Positive control: a move past the margin leaves the fat box.
  broadPhase.moveProxy(proxy, boxAt({ 1.0f, 0, 0 }));
  broadPhase.updatePairs();
  EXPECT_EQ(broadPhase.getLastMovedCount(), 1u);
}

TEST(BroadPhase, ASteadyMoverIsStretchedAheadAndReinsertedOnlyEveryFewTicks)
{
  BroadPhase broadPhase;
  constexpr float step = 0.3f;
  const auto proxy = broadPhase.createProxy(boxAt({ 0, 0, 0 }), true, 0);
  broadPhase.updatePairs();

  broadPhase.moveProxy(proxy, boxAt({ step, 0, 0 }));
  broadPhase.updatePairs();
  ASSERT_EQ(broadPhase.getLastMovedCount(), 1u);

  // The fat box reaches the multiplier's worth of steps ahead and only the margin behind.
  const auto& fat = broadPhase.getFatBox(proxy);
  EXPECT_NEAR(fat.max.x, step + 0.5f + BroadPhase::margin + BroadPhase::displacementMultiplier * step, 1e-5f);
  EXPECT_NEAR(fat.min.x, step - 0.5f - BroadPhase::margin, 1e-5f);

  size_t reinserts = 0;
  for (int tick = 2; tick < 12; ++tick)
  {
    broadPhase.moveProxy(proxy, boxAt({ step * static_cast<float>(tick), 0, 0 }));
    broadPhase.updatePairs();
    reinserts += broadPhase.getLastMovedCount();
  }

  EXPECT_LE(reinserts, 3u);
  EXPECT_GE(reinserts, 1u);
}

TEST(BroadPhase, AFatBoxLeftLargeByAFastMoveShrinksOnceTheMoverStops)
{
  BroadPhase broadPhase;
  const auto proxy = broadPhase.createProxy(boxAt({ 0, 0, 0 }), true, 0);
  broadPhase.updatePairs();

  broadPhase.moveProxy(proxy, boxAt({ 2.0f, 0, 0 }));
  broadPhase.updatePairs();
  const float stretched = broadPhase.getFatBox(proxy).max.x;
  ASSERT_GT(stretched, 9.0f);

  // Standing still inside a box stretched for that speed, it is refitted to the margin alone.
  broadPhase.moveProxy(proxy, boxAt({ 2.0f, 0, 0 }));
  broadPhase.updatePairs();

  EXPECT_EQ(broadPhase.getLastMovedCount(), 1u);
  EXPECT_NEAR(broadPhase.getFatBox(proxy).max.x, 2.5f + BroadPhase::margin, 1e-5f);
}

TEST(BroadPhase, AMovingStaticProxyPairsWithTheDynamicOneItReaches)
{
  BroadPhase broadPhase;
  const auto resting = broadPhase.createProxy(boxAt({ 0, 0, 0 }), true, 0);
  const auto platform = broadPhase.createProxy(boxAt({ 5, 0, 0 }), false, 1);
  broadPhase.updatePairs();
  ASSERT_TRUE(broadPhase.getPairs().empty());

  broadPhase.moveProxy(platform, boxAt({ 0.8f, 0, 0 }));
  broadPhase.updatePairs();

  EXPECT_TRUE(hasPair(broadPhase, resting, platform));

  broadPhase.moveProxy(platform, boxAt({ 5, 0, 0 }));
  broadPhase.updatePairs();

  EXPECT_TRUE(broadPhase.getPairs().empty());
}

TEST(BroadPhase, ADestroyedProxyTakesItsPairsAndItsIdDoesNotHandThemOn)
{
  BroadPhase broadPhase;
  const auto kept = broadPhase.createProxy(boxAt({ 0, 0, 0 }), true, 0);
  const auto doomed = broadPhase.createProxy(boxAt({ 0.5f, 0, 0 }), true, 1);
  broadPhase.updatePairs();
  ASSERT_TRUE(hasPair(broadPhase, kept, doomed));

  broadPhase.destroyProxy(doomed);

  // Created before the pairs are refreshed, so it must not take the destroyed id yet.
  const auto newcomer = broadPhase.createProxy(boxAt({ 30, 0, 0 }), true, 2);
  EXPECT_NE(newcomer, doomed);
  broadPhase.updatePairs();

  EXPECT_TRUE(broadPhase.getPairs().empty());
  EXPECT_EQ(broadPhase.getProxyCount(), 2u);

  // Once refreshed the id is free again, and a proxy reusing it starts with none of the old pairs.
  const auto reused = broadPhase.createProxy(boxAt({ -30, 0, 0 }), true, 3);
  EXPECT_EQ(reused, doomed);
  broadPhase.updatePairs();

  EXPECT_TRUE(broadPhase.getPairs().empty());
  EXPECT_EQ(broadPhase.getUserData(reused), 3);
}

TEST(BroadPhase, ANonFiniteBoxDropsOutOfEveryPairUntilItIsFiniteAgain)
{
  BroadPhase broadPhase;
  const auto body = broadPhase.createProxy(boxAt({ 0, 0, 0 }), true, 0);
  const auto ground = broadPhase.createProxy(boxAt({ 0, -0.5f, 0 }, 1.0f), false, 1);
  broadPhase.updatePairs();
  ASSERT_TRUE(hasPair(broadPhase, body, ground));

  Aabb blownUp = boxAt({ 0, 0, 0 });
  blownUp.min.x = std::numeric_limits<float>::quiet_NaN();
  broadPhase.moveProxy(body, blownUp);
  broadPhase.updatePairs();

  EXPECT_TRUE(broadPhase.getPairs().empty());
  EXPECT_EQ(broadPhase.getDynamicTree().getLeafCount(), 0u);
  EXPECT_TRUE(broadPhase.getStaticTree().isValid());

  broadPhase.moveProxy(body, boxAt({ 0, 0, 0 }));
  broadPhase.updatePairs();

  EXPECT_TRUE(hasPair(broadPhase, body, ground));
}

TEST(BroadPhase, TheCacheMatchesBruteForceThroughMotionSpawnsAndDestroys)
{
  BroadPhase broadPhase;
  Lcg random;
  std::vector<Body> bodies;

  const auto spawn = [&](const bool dynamic) {
    Body body;
    body.tight = boxAt({ random.next(-12, 12), random.next(-3, 3), random.next(-12, 12) }, random.next(0.2f, 1.5f));
    body.dynamic = dynamic;
    body.alive = true;
    body.proxy = broadPhase.createProxy(body.tight, dynamic, static_cast<int32_t>(bodies.size()));
    bodies.push_back(body);
  };

  for (int i = 0; i < 40; ++i)
  {
    spawn(false);
  }

  for (int i = 0; i < 160; ++i)
  {
    spawn(true);
  }

  // A wide static floor that every falling body ends up touching.
  {
    Body floor;
    floor.tight = { { -20, -6, -20 }, { 20, -5, 20 } };
    floor.alive = true;
    floor.proxy = broadPhase.createProxy(floor.tight, false, static_cast<int32_t>(bodies.size()));
    bodies.push_back(floor);
  }

  broadPhase.updatePairs();
  expectCacheExact(broadPhase, bodies, 0);

  for (int tick = 1; tick <= 120; ++tick)
  {
    for (auto& body : bodies)
    {
      if (!body.alive || !body.dynamic)
      {
        continue;
      }

      // Mostly small drifts and falls, with the odd teleport across the scene.
      glm::vec3 step{ random.next(-0.08f, 0.08f), random.next(-0.25f, 0.02f), random.next(-0.08f, 0.08f) };
      if (random.next(0, 1) < 0.01f)
      {
        step = { random.next(-15, 15), 0.0f, random.next(-15, 15) };
      }

      if (body.tight.min.y + step.y < -5.0f)
      {
        step.y = -5.0f - body.tight.min.y;
      }

      body.tight.min += step;
      body.tight.max += step;
      broadPhase.moveProxy(body.proxy, body.tight);
    }

    // Statics occasionally move too, like a platform a script drives.
    if (tick % 10 == 0)
    {
      auto& platform = bodies[static_cast<size_t>(tick / 10)];
      const glm::vec3 shift{ random.next(-2, 2), 0.0f, random.next(-2, 2) };
      platform.tight.min += shift;
      platform.tight.max += shift;
      broadPhase.moveProxy(platform.proxy, platform.tight);
    }

    if (tick % 7 == 0)
    {
      auto& doomed = bodies[static_cast<size_t>(40 + tick)];
      broadPhase.destroyProxy(doomed.proxy);
      doomed.alive = false;
      spawn(tick % 2 == 0);
    }

    broadPhase.updatePairs();
    expectCacheExact(broadPhase, bodies, tick);
    if (HasFatalFailure())
    {
      return;
    }
  }
}

TEST(BroadPhase, ClearForgetsEveryProxyAndPair)
{
  BroadPhase broadPhase;
  static_cast<void>(broadPhase.createProxy(boxAt({ 0, 0, 0 }), true, 0));
  static_cast<void>(broadPhase.createProxy(boxAt({ 0.5f, 0, 0 }), true, 1));
  broadPhase.updatePairs();
  ASSERT_FALSE(broadPhase.getPairs().empty());

  broadPhase.clear();

  EXPECT_TRUE(broadPhase.getPairs().empty());
  EXPECT_EQ(broadPhase.getProxyCount(), 0u);
  EXPECT_EQ(broadPhase.getDynamicTree().getLeafCount(), 0u);
}
