#include <gtest/gtest.h>

#include <AssetFilter.h>
#include <assets/AssetRegistry.h>

#include <array>

namespace {
  constexpr std::array concreteTypes {
    AssetType::Model, AssetType::Texture, AssetType::Scene, AssetType::Script, AssetType::Prefab
  };
}

TEST(AssetTypeFilter, DefaultIsEmptyAndMatchesEveryType)
{
  const AssetTypeFilter filter;

  EXPECT_TRUE(filter.empty());
  EXPECT_EQ(filter.count(), 0);
  for (const auto type : concreteTypes)
  {
    EXPECT_TRUE(filter.matches(type));
    EXPECT_FALSE(filter.isActive(type));
  }
}

TEST(AssetTypeFilter, OneTypeMatchesOnlyThatType)
{
  AssetTypeFilter filter;
  filter.toggle(AssetType::Model);

  EXPECT_TRUE(filter.matches(AssetType::Model));
  EXPECT_TRUE(filter.isActive(AssetType::Model));
  EXPECT_FALSE(filter.empty());
  for (const auto type : concreteTypes)
  {
    if (type != AssetType::Model)
    {
      EXPECT_FALSE(filter.matches(type));
    }
  }
}

TEST(AssetTypeFilter, TwoTypesMatchTheUnionAndNothingElse)
{
  AssetTypeFilter filter;
  filter.toggle(AssetType::Model);
  filter.toggle(AssetType::Prefab);

  EXPECT_TRUE(filter.matches(AssetType::Model));
  EXPECT_TRUE(filter.matches(AssetType::Prefab));
  EXPECT_FALSE(filter.matches(AssetType::Texture));
  EXPECT_FALSE(filter.matches(AssetType::Scene));
  EXPECT_FALSE(filter.matches(AssetType::Script));
}

TEST(AssetTypeFilter, TogglingTwiceRemovesTheType)
{
  AssetTypeFilter filter;
  filter.toggle(AssetType::Scene);
  ASSERT_TRUE(filter.isActive(AssetType::Scene));

  filter.toggle(AssetType::Scene);

  EXPECT_FALSE(filter.isActive(AssetType::Scene));
  EXPECT_TRUE(filter.empty());
  EXPECT_EQ(filter, AssetTypeFilter{});
}

TEST(AssetTypeFilter, SetAddsAndRemovesWithoutFlipping)
{
  AssetTypeFilter filter;
  filter.set(AssetType::Script, true);
  filter.set(AssetType::Script, true);
  EXPECT_TRUE(filter.isActive(AssetType::Script));
  EXPECT_EQ(filter.count(), 1);

  filter.set(AssetType::Script, false);
  filter.set(AssetType::Script, false);
  EXPECT_FALSE(filter.isActive(AssetType::Script));
  EXPECT_EQ(filter.count(), 0);
}

TEST(AssetTypeFilter, ClearReturnsToMatchAll)
{
  AssetTypeFilter filter;
  filter.toggle(AssetType::Model);
  filter.toggle(AssetType::Texture);
  ASSERT_FALSE(filter.matches(AssetType::Scene));

  filter.clear();

  EXPECT_TRUE(filter.empty());
  for (const auto type : concreteTypes)
  {
    EXPECT_TRUE(filter.matches(type));
  }
}

TEST(AssetTypeFilter, UnknownIsNeverAdded)
{
  AssetTypeFilter filter;
  filter.toggle(AssetType::Unknown);
  filter.set(AssetType::Unknown, true);

  EXPECT_TRUE(filter.empty());
  EXPECT_FALSE(filter.isActive(AssetType::Unknown));
  EXPECT_EQ(filter.count(), 0);

  filter.toggle(AssetType::Model);
  EXPECT_EQ(filter.count(), 1);
  EXPECT_FALSE(filter.matches(AssetType::Unknown));
}

TEST(AssetTypeFilter, CountTracksMembership)
{
  AssetTypeFilter filter;
  int expected = 0;
  for (const auto type : concreteTypes)
  {
    filter.toggle(type);
    ++expected;
    EXPECT_EQ(filter.count(), expected);
  }

  filter.toggle(AssetType::Model);
  EXPECT_EQ(filter.count(), expected - 1);
}

TEST(AssetTypeFilter, EqualityComparesMembership)
{
  AssetTypeFilter a;
  AssetTypeFilter b;
  a.toggle(AssetType::Model);
  EXPECT_NE(a, b);

  b.toggle(AssetType::Model);
  EXPECT_EQ(a, b);
}
