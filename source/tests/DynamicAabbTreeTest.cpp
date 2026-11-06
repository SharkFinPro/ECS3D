#include <gtest/gtest.h>

#include "broadphase/DynamicAabbTree.h"

#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace {
  struct Lcg {
    uint32_t state = 7u;

    float next(const float low, const float high)
    {
      state = state * 1664525u + 1013904223u;
      return low + (high - low) * static_cast<float>((state >> 8) & 0xFFFFu) / 65535.0f;
    }
  };

  Aabb boxAt(const glm::vec3& center, const float halfSize)
  {
    return { center - glm::vec3(halfSize), center + glm::vec3(halfSize) };
  }

  Aabb randomBox(Lcg& random)
  {
    return boxAt({ random.next(-20, 20), random.next(-20, 20), random.next(-20, 20) }, random.next(0.1f, 2.0f));
  }

  std::vector<int32_t> queried(const DynamicAabbTree& tree, const Aabb& box)
  {
    std::vector<int32_t> stack;
    std::vector<int32_t> found;
    tree.query(box, stack, [&found](const int32_t userData) { found.push_back(userData); });
    std::ranges::sort(found);
    return found;
  }

  // Every live box the query overlaps, by brute force.
  std::vector<int32_t> bruteForceQuery(const std::vector<Aabb>& boxes, const std::vector<char>& live, const Aabb& box)
  {
    std::vector<int32_t> found;
    for (size_t i = 0; i < boxes.size(); ++i)
    {
      if (live[i] && boxes[i].overlaps(box))
      {
        found.push_back(static_cast<int32_t>(i));
      }
    }

    return found;
  }
}

TEST(DynamicAabbTree, AnEmptyTreeFindsNothing)
{
  const DynamicAabbTree tree;

  EXPECT_TRUE(queried(tree, boxAt({ 0, 0, 0 }, 100.0f)).empty());
  EXPECT_EQ(tree.getHeight(), 0);
  EXPECT_EQ(tree.getLeafCount(), 0u);
  EXPECT_TRUE(tree.isValid());
}

TEST(DynamicAabbTree, BoxesThatOnlyTouchOverlap)
{
  DynamicAabbTree tree;
  static_cast<void>(tree.insert({ { 0, 0, 0 }, { 1, 1, 1 } }, 4));

  EXPECT_EQ(queried(tree, { { 1, 0, 0 }, { 2, 1, 1 } }), std::vector<int32_t>{ 4 });

  // Positive control the other way: a hair past the shared face finds nothing.
  EXPECT_TRUE(queried(tree, { { 1.0001f, 0, 0 }, { 2, 1, 1 } }).empty());
}

TEST(DynamicAabbTree, QueriesMatchABruteForceScanAsLeavesArrive)
{
  DynamicAabbTree tree;
  Lcg random;
  std::vector<Aabb> boxes;
  std::vector<char> live;

  for (int i = 0; i < 300; ++i)
  {
    boxes.push_back(randomBox(random));
    live.push_back(1);
    static_cast<void>(tree.insert(boxes.back(), i));
    ASSERT_TRUE(tree.isValid()) << "after insert " << i;
  }

  for (int query = 0; query < 200; ++query)
  {
    const glm::vec3 center{ random.next(-20, 20), random.next(-20, 20), random.next(-20, 20) };
    const Aabb box = boxAt(center, random.next(0.5f, 6.0f));
    EXPECT_EQ(queried(tree, box), bruteForceQuery(boxes, live, box)) << "query " << query;
  }

  EXPECT_EQ(tree.getLeafCount(), 300u);
}

TEST(DynamicAabbTree, RemovingAndMovingLeavesKeepsQueriesExact)
{
  DynamicAabbTree tree;
  Lcg random;
  std::vector<Aabb> boxes;
  std::vector<char> live;
  std::vector<int32_t> leaves;

  for (int i = 0; i < 200; ++i)
  {
    boxes.push_back(randomBox(random));
    live.push_back(1);
    leaves.push_back(tree.insert(boxes.back(), i));
  }

  for (int i = 0; i < 200; i += 2)
  {
    tree.remove(leaves[static_cast<size_t>(i)]);
    live[static_cast<size_t>(i)] = 0;
    ASSERT_TRUE(tree.isValid()) << "after removing " << i;
  }

  for (int i = 1; i < 200; i += 4)
  {
    boxes[static_cast<size_t>(i)] = randomBox(random);
    tree.move(leaves[static_cast<size_t>(i)], boxes[static_cast<size_t>(i)]);
    ASSERT_TRUE(tree.isValid()) << "after moving " << i;
  }

  for (int query = 0; query < 200; ++query)
  {
    const glm::vec3 center{ random.next(-20, 20), random.next(-20, 20), random.next(-20, 20) };
    const Aabb box = boxAt(center, random.next(0.5f, 6.0f));
    EXPECT_EQ(queried(tree, box), bruteForceQuery(boxes, live, box)) << "query " << query;
  }

  EXPECT_EQ(tree.getLeafCount(), 100u);
}

TEST(DynamicAabbTree, AMovedLeafKeepsItsIdAndAFreedNodeIsReused)
{
  DynamicAabbTree tree;
  const int32_t first = tree.insert(boxAt({ 0, 0, 0 }, 1.0f), 10);
  const int32_t second = tree.insert(boxAt({ 5, 0, 0 }, 1.0f), 11);

  tree.move(first, boxAt({ 50, 0, 0 }, 1.0f));

  EXPECT_EQ(tree.getUserData(first), 10);
  EXPECT_EQ(tree.getBox(first).min.x, 49.0f);
  EXPECT_EQ(queried(tree, boxAt({ 50, 0, 0 }, 0.5f)), std::vector<int32_t>{ 10 });
  EXPECT_TRUE(queried(tree, boxAt({ 0, 0, 0 }, 0.5f)).empty());

  tree.remove(second);
  const int32_t third = tree.insert(boxAt({ -5, 0, 0 }, 1.0f), 12);

  // The tree grows no further than the two leaves and one parent it needed at most.
  EXPECT_LT(third, 3);
  EXPECT_TRUE(tree.isValid());
}

TEST(DynamicAabbTree, BoxesArrivingInSortedOrderStayShallow)
{
  // Sorted arrival is what turns an unbalanced tree into a list; the rotations keep it logarithmic.
  DynamicAabbTree tree;
  constexpr int count = 1024;
  for (int i = 0; i < count; ++i)
  {
    static_cast<void>(tree.insert(boxAt({ static_cast<float>(i) * 1.5f, 0, 0 }, 0.5f), i));
  }

  ASSERT_TRUE(tree.isValid());
  EXPECT_LE(tree.getHeight(), 3 * static_cast<int>(std::log2(count)));
  EXPECT_GE(tree.getHeight(), static_cast<int>(std::log2(count)));
}

TEST(DynamicAabbTree, ClearEmptiesTheTree)
{
  DynamicAabbTree tree;
  static_cast<void>(tree.insert(boxAt({ 0, 0, 0 }, 1.0f), 1));
  static_cast<void>(tree.insert(boxAt({ 1, 0, 0 }, 1.0f), 2));

  tree.clear();

  EXPECT_EQ(tree.getLeafCount(), 0u);
  EXPECT_TRUE(queried(tree, boxAt({ 0, 0, 0 }, 10.0f)).empty());
  EXPECT_TRUE(tree.isValid());
}

TEST(Aabb, IsFiniteOnlyWhenEveryBoundIs)
{
  EXPECT_TRUE(boxAt({ 1, 2, 3 }, 1.0f).isFinite());

  Aabb withNan = boxAt({ 0, 0, 0 }, 1.0f);
  withNan.max.y = std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(withNan.isFinite());

  Aabb withInfinity = boxAt({ 0, 0, 0 }, 1.0f);
  withInfinity.min.z = -std::numeric_limits<float>::infinity();
  EXPECT_FALSE(withInfinity.isFinite());
}
