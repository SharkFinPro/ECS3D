#include <gtest/gtest.h>

#include "broadphase/DynamicAabbTree.h"

#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <set>
#include <vector>

namespace {
  struct Item {
    Aabb tight;
    int32_t proxy = DynamicAabbTree::nullNode;
    bool alive = false;
  };

  Aabb boxAround(const glm::vec3& center, const glm::vec3& halfSize)
  {
    return { center - halfSize, center + halfSize };
  }

  class TreeFuzz {
  public:
    TreeFuzz() : m_random(12345) {}

    float uniform(const float lo, const float hi)
    {
      return std::uniform_real_distribution<float>(lo, hi)(m_random);
    }

    int index(const int count)
    {
      return std::uniform_int_distribution<int>(0, count - 1)(m_random);
    }

    glm::vec3 randomVec(const float lo, const float hi)
    {
      return { uniform(lo, hi), uniform(lo, hi), uniform(lo, hi) };
    }

    Aabb randomBox(const float center, const float minHalf, const float maxHalf)
    {
      return boxAround(randomVec(-center, center), randomVec(minHalf, maxHalf));
    }

    void create(DynamicAabbTree& tree, std::vector<Item>& items, const Aabb& tight)
    {
      Item item;
      item.tight = tight;
      item.alive = true;
      items.push_back(item);

      const auto id = static_cast<int32_t>(items.size()) - 1;
      items.back().proxy = tree.createProxy(tight, id, randomVec(-0.05f, 0.05f));
    }

    std::mt19937& engine() { return m_random; }

  private:
    std::mt19937 m_random;
  };

  void expectQueriesMatchBruteForce(TreeFuzz& fuzz, const DynamicAabbTree& tree, const std::vector<Item>& items)
  {
    for (int q = 0; q < 25; ++q)
    {
      const Aabb query = fuzz.randomBox(60.0f, 0.5f, 12.0f);

      std::set<int32_t> fromTree;
      tree.query(query, [&](const int32_t proxy)
      {
        fromTree.insert(tree.getUserData(proxy));
        return true;
      });

      std::set<int32_t> expected;
      for (size_t i = 0; i < items.size(); ++i)
      {
        if (items[i].alive && tree.getFatAabb(items[i].proxy).overlaps(query))
        {
          expected.insert(static_cast<int32_t>(i));
        }
      }

      EXPECT_EQ(fromTree, expected);
    }
  }
}

TEST(DynamicAabbTree, QueriesMatchABruteForceScanThroughCreateMoveAndDestroy)
{
  TreeFuzz fuzz;
  DynamicAabbTree tree;
  std::vector<Item> items;

  for (int i = 0; i < 300; ++i)
  {
    fuzz.create(tree, items, fuzz.randomBox(50.0f, 0.125f, 1.5f));
  }

  fuzz.create(tree, items, boxAround({ 0.0f, -10.0f, 0.0f }, { 100.0f, 10.0f, 100.0f }));

  for (int round = 0; round < 30; ++round)
  {
    for (auto& item : items)
    {
      if (!item.alive)
      {
        continue;
      }

      const int roll = fuzz.index(10);
      if (roll < 4)
      {
        const glm::vec3 shift = fuzz.randomVec(-0.3f, 0.3f);
        item.tight = { item.tight.min + shift, item.tight.max + shift };
        tree.moveProxy(item.proxy, item.tight, fuzz.randomVec(-0.1f, 0.1f));
      }
      else if (roll == 4)
      {
        const glm::vec3 half = (item.tight.max - item.tight.min) * 0.5f;
        item.tight = boxAround(fuzz.randomVec(-50.0f, 50.0f), half);
        tree.moveProxy(item.proxy, item.tight, glm::vec3(0.0f));
      }
      else if (roll == 5 && fuzz.index(3) == 0)
      {
        tree.destroyProxy(item.proxy);
        item.alive = false;
      }
    }

    for (int i = 0; i < 20; ++i)
    {
      fuzz.create(tree, items, fuzz.randomBox(50.0f, 0.125f, 1.5f));
    }

    size_t alive = 0;
    for (const auto& item : items)
    {
      if (item.alive)
      {
        ++alive;
        EXPECT_TRUE(tree.getFatAabb(item.proxy).contains(item.tight));
      }
    }

    EXPECT_EQ(tree.getProxyCount(), alive);
    expectQueriesMatchBruteForce(fuzz, tree, items);
  }
}

TEST(DynamicAabbTree, AProxyMovingInsideItsMarginIsNotReinserted)
{
  DynamicAabbTree tree;
  const Aabb tight = boxAround({ 0.0f, 0.0f, 0.0f }, { 0.5f, 0.5f, 0.5f });
  const auto proxy = tree.createProxy(tight, 7, glm::vec3(0.0f));

  const Aabb nudged = boxAround({ 0.02f, -0.02f, 0.01f }, { 0.5f, 0.5f, 0.5f });
  EXPECT_FALSE(tree.moveProxy(proxy, nudged, glm::vec3(0.0f)));
  EXPECT_TRUE(tree.getFatAabb(proxy).contains(nudged));

  const Aabb jumped = boxAround({ 3.0f, 0.0f, 0.0f }, { 0.5f, 0.5f, 0.5f });
  EXPECT_TRUE(tree.moveProxy(proxy, jumped, glm::vec3(0.0f)));
  EXPECT_TRUE(tree.getFatAabb(proxy).contains(jumped));
  EXPECT_EQ(tree.getUserData(proxy), 7);
}

TEST(DynamicAabbTree, AProxyMovingAtASteadyVelocityIsNotReinsertedEveryStep)
{
  DynamicAabbTree tree;
  Aabb tight = boxAround({ 0.0f, 0.0f, 0.0f }, { 0.5f, 0.5f, 0.5f });
  const glm::vec3 step(0.0f, -0.15f, 0.0f);
  const auto proxy = tree.createProxy(tight, 0, step);

  int reinserts = 0;
  for (int i = 0; i < 20; ++i)
  {
    tight = { tight.min + step, tight.max + step };
    if (tree.moveProxy(proxy, tight, step))
    {
      ++reinserts;
    }

    EXPECT_TRUE(tree.getFatAabb(proxy).contains(tight));
  }

  EXPECT_GT(reinserts, 0);
  EXPECT_LE(reinserts, 8);
}

TEST(DynamicAabbTree, TheFatBoxLeadsInTheDirectionOfMotion)
{
  DynamicAabbTree tree;
  const Aabb tight = boxAround({ 0.0f, 0.0f, 0.0f }, { 0.5f, 0.5f, 0.5f });
  const auto proxy = tree.createProxy(tight, 0, { 0.25f, -0.25f, 0.0f });

  const Aabb& fat = tree.getFatAabb(proxy);
  const float margin = DynamicAabbTree::margin;
  const float lead = DynamicAabbTree::displacementMultiplier * 0.25f;

  EXPECT_FLOAT_EQ(fat.max.x, 0.5f + margin + lead);
  EXPECT_FLOAT_EQ(fat.min.x, -0.5f - margin);
  EXPECT_FLOAT_EQ(fat.min.y, -0.5f - margin - lead);
  EXPECT_FLOAT_EQ(fat.max.y, 0.5f + margin);
}

TEST(DynamicAabbTree, HeightStaysLogarithmicForBoxesInsertedInASortedLine)
{
  DynamicAabbTree tree;

  constexpr int count = 256;
  for (int i = 0; i < count; ++i)
  {
    const auto x = static_cast<float>(i) * 2.0f;
    static_cast<void>(tree.createProxy(boxAround({ x, 0.0f, 0.0f }, { 0.5f, 0.5f, 0.5f }), i, glm::vec3(0.0f)));
  }

  const int bound = 2 * static_cast<int>(std::ceil(std::log2(static_cast<double>(count)))) + 2;
  EXPECT_LE(tree.getHeight(), bound);
  EXPECT_EQ(tree.getProxyCount(), static_cast<size_t>(count));
}

TEST(DynamicAabbTree, ClearEmptiesTheTree)
{
  DynamicAabbTree tree;
  static_cast<void>(tree.createProxy(boxAround({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }), 0, glm::vec3(0.0f)));
  tree.clear();

  int hits = 0;
  tree.query(boxAround({ 0.0f, 0.0f, 0.0f }, { 5.0f, 5.0f, 5.0f }), [&](int32_t)
  {
    ++hits;
    return true;
  });

  EXPECT_EQ(hits, 0);
  EXPECT_EQ(tree.getProxyCount(), 0u);
  EXPECT_EQ(tree.getHeight(), 0);
}
