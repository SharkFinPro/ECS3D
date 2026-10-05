#include <gtest/gtest.h>

#include <UnsavedChanges.h>

TEST(UnsavedChanges, StartsClean)
{
  const UnsavedChanges changes;

  EXPECT_FALSE(changes.isDirty());
}

TEST(UnsavedChanges, AnEditMakesItDirtyAndASaveMakesItCleanAgain)
{
  UnsavedChanges changes;

  changes.markEdited();
  EXPECT_TRUE(changes.isDirty());

  changes.markSaved();
  EXPECT_FALSE(changes.isDirty());
}

TEST(UnsavedChanges, AnEditAfterASaveIsDirtyAgain)
{
  UnsavedChanges changes;
  changes.markEdited();
  changes.markSaved();
  ASSERT_FALSE(changes.isDirty());

  changes.markEdited();

  EXPECT_TRUE(changes.isDirty());
}

TEST(UnsavedChanges, SeveralEditsNeedOnlyOneSave)
{
  UnsavedChanges changes;
  changes.markEdited();
  changes.markEdited();
  changes.markEdited();
  ASSERT_TRUE(changes.isDirty());

  changes.markSaved();

  EXPECT_FALSE(changes.isDirty());
}

TEST(UnsavedChanges, SavingACleanProjectLeavesItClean)
{
  UnsavedChanges changes;

  changes.markSaved();

  EXPECT_FALSE(changes.isDirty());
}

TEST(DecideDiscard, CoversEveryCombination)
{
  EXPECT_EQ(decideDiscard(false, false), DiscardDecision::performNow);
  EXPECT_EQ(decideDiscard(false, true), DiscardDecision::performNow);
  EXPECT_EQ(decideDiscard(true, false), DiscardDecision::prompt);
  EXPECT_EQ(decideDiscard(true, true), DiscardDecision::ignore);
}
