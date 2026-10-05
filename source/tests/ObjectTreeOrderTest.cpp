#include <gtest/gtest.h>

#include "TestScene.h"
#include <ObjectTreeOrder.h>
#include <Selection.h>

#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include <uuid.h>

namespace {
  using namespace objectTreeOrder;

  uuids::uuid makeId(const int n)
  {
    char text[40];
    std::snprintf(text, sizeof(text), "00000000-0000-0000-0000-%012d", n);
    return uuids::uuid::from_string(text).value();
  }

  const std::vector<uuids::uuid> order { makeId(1), makeId(2), makeId(3), makeId(4), makeId(5) };

  std::vector<std::string> namesOf(const std::vector<std::shared_ptr<Object>>& objects)
  {
    std::vector<std::string> names;
    for (const auto& object : objects)
    {
      names.push_back(object->getName());
    }
    return names;
  }
}

TEST(ObjectTreeRange, AnchorBeforeClickedIsForwardInclusiveSlice)
{
  const auto range = rangeBetween(order, makeId(2), makeId(4));

  EXPECT_EQ(range, (std::vector { makeId(2), makeId(3), makeId(4) }));
}

TEST(ObjectTreeRange, AnchorAfterClickedRunsAnchorToClickedWithClickedLast)
{
  const auto range = rangeBetween(order, makeId(4), makeId(2));

  EXPECT_EQ(range, (std::vector { makeId(4), makeId(3), makeId(2) }));
  EXPECT_EQ(range.back(), makeId(2));
}

TEST(ObjectTreeRange, AnchorEqualToClickedIsJustThatRow)
{
  EXPECT_EQ(rangeBetween(order, makeId(3), makeId(3)), (std::vector { makeId(3) }));
}

TEST(ObjectTreeRange, MissingAnchorFallsBackToClicked)
{
  EXPECT_EQ(rangeBetween(order, makeId(99), makeId(3)), (std::vector { makeId(3) }));
  // Positive control: the same click with a present anchor does produce a range.
  EXPECT_EQ(rangeBetween(order, makeId(1), makeId(3)).size(), 3u);
}

TEST(ObjectTreeRange, MissingClickedFallsBackToClicked)
{
  EXPECT_EQ(rangeBetween(order, makeId(1), makeId(99)), (std::vector { makeId(99) }));
}

TEST(ObjectTreeActionTargets, SingleSelectionContainingRowIsJustTheRow)
{
  EditorSelection selection;
  selection.selectObject(makeId(1));

  EXPECT_EQ(actionTargets(selection, makeId(1)), (std::vector { makeId(1) }));
}

TEST(ObjectTreeActionTargets, MultiSelectionIncludingRowIsWholeSelectionInOrder)
{
  EditorSelection selection;
  selection.selectObject(makeId(3));
  selection.addObject(makeId(1));
  selection.addObject(makeId(2));

  EXPECT_EQ(actionTargets(selection, makeId(1)), (std::vector { makeId(3), makeId(1), makeId(2) }));
}

TEST(ObjectTreeActionTargets, MultiSelectionNotIncludingRowIsJustTheRow)
{
  EditorSelection selection;
  selection.selectObject(makeId(1));
  selection.addObject(makeId(2));

  EXPECT_EQ(actionTargets(selection, makeId(5)), (std::vector { makeId(5) }));
  // Positive control: a row inside the same selection takes the whole selection.
  EXPECT_EQ(actionTargets(selection, makeId(2)).size(), 2u);
}

TEST(ObjectTreeActionTargets, AssetSelectionIsJustTheRow)
{
  EditorSelection selection;
  selection.selectAsset(makeId(1));
  selection.addAsset(makeId(2));

  EXPECT_EQ(actionTargets(selection, makeId(1)), (std::vector { makeId(1) }));
}

TEST(ObjectTreeSort, AuthoredReturnsInputUnchangedAndLeavesScratchAlone)
{
  const auto scene = fixtures::makeScene();
  const std::vector<std::shared_ptr<Object>> objects {
    addObject(scene, "cherry"), addObject(scene, "apple")
  };
  std::vector<std::shared_ptr<Object>> scratch;

  const auto& result = sortedForDisplay(objects, SortMode::authored, scratch);

  EXPECT_EQ(&result, &objects);
  EXPECT_TRUE(scratch.empty());
}

TEST(ObjectTreeSort, AlphabeticalIsCaseInsensitive)
{
  const auto scene = fixtures::makeScene();
  const std::vector<std::shared_ptr<Object>> objects {
    addObject(scene, "cherry"), addObject(scene, "Banana"), addObject(scene, "apple")
  };
  std::vector<std::shared_ptr<Object>> scratch;

  const auto& result = sortedForDisplay(objects, SortMode::alphabetical, scratch);

  EXPECT_EQ(namesOf(result), (std::vector<std::string> { "apple", "Banana", "cherry" }));
  EXPECT_EQ(&result, &scratch);
  EXPECT_EQ(namesOf(objects), (std::vector<std::string> { "cherry", "Banana", "apple" }));
}

TEST(ObjectTreeSort, AlphabeticalKeepsAuthoredOrderForEqualNames)
{
  // Enough elements that an unstable sort would not fall back to insertion sort and happen to stay stable.
  const auto scene = fixtures::makeScene();
  const std::array<const char*, 4> names { "b", "B", "a", "A" };
  std::vector<std::shared_ptr<Object>> objects;
  for (int i = 0; i < 24; ++i)
  {
    objects.push_back(addObject(scene, names[i % names.size()]));
  }
  std::vector<std::shared_ptr<Object>> scratch;

  const auto& result = sortedForDisplay(objects, SortMode::alphabetical, scratch);

  std::vector<uuids::uuid> expected;
  for (const bool aClass : { true, false })
  {
    for (int i = 0; i < 24; ++i)
    {
      if ((i % 4 >= 2) == aClass)
      {
        expected.push_back(objects[i]->getUUID());
      }
    }
  }
  std::vector<uuids::uuid> actual;
  for (const auto& object : result)
  {
    actual.push_back(object->getUUID());
  }
  EXPECT_EQ(actual, expected);
}

TEST(ObjectTreeSort, NonAsciiBytesCompareByUnsignedValue)
{
  // 0xC3 is a negative char on signed-char platforms; unsigned comparison puts it after 'z'.
  const std::string accented = "\xC3\xA9";
  EXPECT_TRUE(ciNameLess("z", accented));
  EXPECT_FALSE(ciNameLess(accented, "z"));
  EXPECT_TRUE(ciNameLess("A", "b"));
}

TEST(ObjectTreeSort, SortModeStringRoundTrips)
{
  for (const auto mode : { SortMode::authored, SortMode::alphabetical })
  {
    EXPECT_EQ(parseSortMode(sortModeToString(mode)), mode);
  }
  EXPECT_STREQ(sortModeToString(SortMode::alphabetical), "alphabetical");
  EXPECT_STREQ(sortModeToString(SortMode::authored), "authored");
  EXPECT_EQ(parseSortMode("alphabetical"), SortMode::alphabetical);
  EXPECT_EQ(parseSortMode("authored"), SortMode::authored);
}

TEST(ObjectTreeSort, UnknownStringParsesToAuthored)
{
  EXPECT_EQ(parseSortMode("sideways"), SortMode::authored);
  EXPECT_EQ(parseSortMode(""), SortMode::authored);
}
