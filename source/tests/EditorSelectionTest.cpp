#include <gtest/gtest.h>

#include <Selection.h>

#include <initializer_list>
#include <optional>
#include <string>
#include <uuid.h>
#include <vector>

namespace {
  uuids::uuid uuidFrom(const std::string& text)
  {
    return uuids::uuid::from_string(text).value();
  }

  const auto idA = uuidFrom("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa");
  const auto idB = uuidFrom("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb");
  const auto idC = uuidFrom("cccccccc-cccc-cccc-cccc-cccccccccccc");

  std::vector<uuids::uuid> itemsOf(const EditorSelection& selection)
  {
    const auto span = selection.items();
    return { span.begin(), span.end() };
  }

  std::vector<uuids::uuid> ids(const std::initializer_list<uuids::uuid> list)
  {
    return list;
  }
}

TEST(EditorSelection, StartsEmptyWithKindNone)
{
  const EditorSelection selection;

  EXPECT_EQ(selection.kind(), EditorSelection::Kind::None);
  EXPECT_TRUE(selection.empty());
  EXPECT_EQ(selection.size(), 0u);
  EXPECT_FALSE(selection.objectUUID().has_value());
  EXPECT_FALSE(selection.assetUUID().has_value());
}

TEST(EditorSelection, SelectObjectReplacesAndSetsKind)
{
  EditorSelection selection;
  selection.selectObject(idA);
  selection.addObject(idB);

  selection.selectObject(idC);

  EXPECT_EQ(selection.kind(), EditorSelection::Kind::Object);
  EXPECT_EQ(itemsOf(selection), ids({ idC }));
  EXPECT_FALSE(selection.contains(idA));
}

TEST(EditorSelection, SelectAssetReplacesObjectSelection)
{
  EditorSelection selection;
  selection.selectObject(idA);

  selection.selectAsset(idB);

  EXPECT_EQ(selection.kind(), EditorSelection::Kind::Asset);
  EXPECT_EQ(itemsOf(selection), ids({ idB }));
  EXPECT_FALSE(selection.contains(idA));
}

TEST(EditorSelection, AddAppendsAndLastAddedIsPrimary)
{
  EditorSelection selection;
  selection.selectObject(idA);
  selection.addObject(idB);
  selection.addObject(idC);

  EXPECT_EQ(itemsOf(selection), ids({ idA, idB, idC }));
  EXPECT_EQ(selection.size(), 3u);
  EXPECT_EQ(selection.objectUUID(), idC);
}

TEST(EditorSelection, ReAddingAnItemMovesItToTheBackWithoutDuplicating)
{
  EditorSelection selection;
  selection.selectObject(idA);
  selection.addObject(idB);
  selection.addObject(idC);

  selection.addObject(idA);

  EXPECT_EQ(itemsOf(selection), ids({ idB, idC, idA }));
  EXPECT_EQ(selection.objectUUID(), idA);
}

TEST(EditorSelection, AddingOtherKindReplacesTheSelection)
{
  EditorSelection selection;
  selection.selectObject(idA);
  selection.addObject(idB);

  selection.addAsset(idC);

  EXPECT_EQ(selection.kind(), EditorSelection::Kind::Asset);
  EXPECT_EQ(itemsOf(selection), ids({ idC }));

  selection.addAsset(idA);
  EXPECT_EQ(itemsOf(selection), ids({ idC, idA }));

  selection.addObject(idB);
  EXPECT_EQ(selection.kind(), EditorSelection::Kind::Object);
  EXPECT_EQ(itemsOf(selection), ids({ idB }));
}

TEST(EditorSelection, ToggleAddsWhenAbsentAndRemovesWhenPresent)
{
  EditorSelection selection;
  selection.selectObject(idA);

  selection.toggleObject(idB);
  EXPECT_EQ(itemsOf(selection), ids({ idA, idB }));

  selection.toggleObject(idB);
  EXPECT_EQ(itemsOf(selection), ids({ idA }));
  EXPECT_EQ(selection.kind(), EditorSelection::Kind::Object);
}

TEST(EditorSelection, TogglingThePrimaryAwayPromotesThePreviousItem)
{
  EditorSelection selection;
  selection.selectObject(idA);
  selection.addObject(idB);
  selection.addObject(idC);

  selection.toggleObject(idC);

  EXPECT_EQ(selection.objectUUID(), idB);
  EXPECT_EQ(itemsOf(selection), ids({ idA, idB }));
}

TEST(EditorSelection, TogglingTheLastItemAwayClearsTheKind)
{
  EditorSelection selection;
  selection.selectObject(idA);

  selection.toggleObject(idA);

  EXPECT_EQ(selection.kind(), EditorSelection::Kind::None);
  EXPECT_TRUE(selection.empty());
  EXPECT_FALSE(selection.objectUUID().has_value());
}

TEST(EditorSelection, ToggleOfOtherKindReplacesEvenWhenUuidIsHeld)
{
  EditorSelection selection;
  selection.selectObject(idA);
  selection.addObject(idB);

  selection.toggleAsset(idA);

  EXPECT_EQ(selection.kind(), EditorSelection::Kind::Asset);
  EXPECT_EQ(itemsOf(selection), ids({ idA }));
}

TEST(EditorSelection, RemoveOfNonMemberIsANoOp)
{
  EditorSelection selection;
  selection.selectObject(idA);
  selection.addObject(idB);

  selection.remove(idC);

  EXPECT_EQ(itemsOf(selection), ids({ idA, idB }));
  EXPECT_EQ(selection.kind(), EditorSelection::Kind::Object);

  selection.remove(idA);
  EXPECT_EQ(itemsOf(selection), ids({ idB }));
}

TEST(EditorSelection, RemoveOfThePrimaryFallsBackToThePreviousItem)
{
  EditorSelection selection;
  selection.selectAsset(idA);
  selection.addAsset(idB);

  selection.remove(idB);

  EXPECT_EQ(selection.assetUUID(), idA);
  EXPECT_EQ(selection.kind(), EditorSelection::Kind::Asset);
}

TEST(EditorSelection, ClearEmptiesTheSelection)
{
  EditorSelection selection;
  selection.selectObject(idA);
  selection.addObject(idB);

  selection.clear();

  EXPECT_EQ(selection.kind(), EditorSelection::Kind::None);
  EXPECT_TRUE(selection.empty());
}

TEST(EditorSelection, PruneMissingDropsRejectedItemsAndKeepsOrder)
{
  EditorSelection selection;
  selection.selectObject(idA);
  selection.addObject(idB);
  selection.addObject(idC);

  selection.pruneMissing([](const uuids::uuid& id) { return id != idB; });

  EXPECT_EQ(itemsOf(selection), ids({ idA, idC }));
  EXPECT_EQ(selection.kind(), EditorSelection::Kind::Object);
  EXPECT_EQ(selection.objectUUID(), idC);
}

TEST(EditorSelection, PruneMissingKeepsEverythingWhenAllExist)
{
  EditorSelection selection;
  selection.selectObject(idA);
  selection.addObject(idB);

  selection.pruneMissing([](const uuids::uuid&) { return true; });

  EXPECT_EQ(itemsOf(selection), ids({ idA, idB }));
}

TEST(EditorSelection, PruneMissingOfEverythingClearsTheKind)
{
  EditorSelection selection;
  selection.selectAsset(idA);
  selection.addAsset(idB);

  selection.pruneMissing([](const uuids::uuid&) { return false; });

  EXPECT_EQ(selection.kind(), EditorSelection::Kind::None);
  EXPECT_TRUE(selection.empty());
}

TEST(EditorSelection, PruneMissingOnEmptySelectionIsANoOp)
{
  EditorSelection selection;

  selection.pruneMissing([](const uuids::uuid&) { return false; });

  EXPECT_EQ(selection.kind(), EditorSelection::Kind::None);
  EXPECT_TRUE(selection.empty());
}

TEST(EditorSelection, PrimaryAccessorsMatchOnlyTheirOwnKind)
{
  EditorSelection selection;
  selection.selectObject(idA);

  EXPECT_EQ(selection.objectUUID(), idA);
  EXPECT_FALSE(selection.assetUUID().has_value());

  selection.selectAsset(idB);

  EXPECT_EQ(selection.assetUUID(), idB);
  EXPECT_FALSE(selection.objectUUID().has_value());
}

TEST(EditorSelection, ContainsReportsMembership)
{
  EditorSelection selection;
  selection.selectObject(idA);
  selection.addObject(idB);

  EXPECT_TRUE(selection.contains(idA));
  EXPECT_TRUE(selection.contains(idB));
  EXPECT_FALSE(selection.contains(idC));
}
