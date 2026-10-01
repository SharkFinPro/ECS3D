#include <gtest/gtest.h>

#include "edits/HistoryScope.h"

#include <optional>
#include <uuid.h>

namespace {
  uuids::uuid sceneA()
  {
    return uuids::uuid::from_string("11111111-1111-4111-8111-111111111111").value();
  }

  uuids::uuid sceneB()
  {
    return uuids::uuid::from_string("22222222-2222-4222-8222-222222222222").value();
  }
}

TEST(HistoryScopeTest, UnscopedThenSceneIsAChange)
{
  edits::HistoryScope scope;
  EXPECT_TRUE(scope.observe(sceneA()));
}

TEST(HistoryScopeTest, SameSceneTwiceIsNotAChangeButADifferentOneIs)
{
  edits::HistoryScope scope;
  ASSERT_TRUE(scope.observe(sceneA()));
  EXPECT_FALSE(scope.observe(sceneA()));
  EXPECT_TRUE(scope.observe(sceneB()));
}

TEST(HistoryScopeTest, ResetSceneObservedAgainIsNotAChange)
{
  edits::HistoryScope scope;
  scope.reset(sceneA());
  EXPECT_FALSE(scope.observe(sceneA()));
  EXPECT_TRUE(scope.observe(sceneB()));
}

TEST(HistoryScopeTest, ResetToUnknownThenSceneIsAChange)
{
  edits::HistoryScope scope;
  scope.reset(std::nullopt);
  EXPECT_TRUE(scope.observe(sceneA()));
  EXPECT_FALSE(scope.observe(sceneA()));
}

TEST(HistoryScopeTest, LosingTheSceneIsAChange)
{
  edits::HistoryScope scope;
  scope.reset(sceneA());
  EXPECT_TRUE(scope.observe(std::nullopt));
  EXPECT_FALSE(scope.observe(std::nullopt));
}

TEST(HistoryScopeTest, ObserveAdoptsTheNewScene)
{
  edits::HistoryScope scope;
  scope.reset(sceneA());
  EXPECT_TRUE(scope.observe(sceneB()));
  EXPECT_FALSE(scope.observe(sceneB()));
  EXPECT_TRUE(scope.observe(sceneA()));
}
