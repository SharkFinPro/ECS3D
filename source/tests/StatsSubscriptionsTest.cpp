#include <gtest/gtest.h>

#include <StatsSubscriptions.h>

#include <cstdint>
#include <vector>

TEST(StatsSubscriptions, ANewSubscriberGetsTheTableOnceAndAgainWhenTheEpochGrows)
{
  net::StatsSubscriptions subscriptions;
  EXPECT_TRUE(subscriptions.empty());

  subscriptions.subscribe(5);
  EXPECT_FALSE(subscriptions.empty());
  EXPECT_TRUE(subscriptions.isSubscribed(5));

  const auto first = subscriptions.takeRecipients(3);
  EXPECT_EQ(first.withNodeTable, (std::vector<int32_t>{5}));
  EXPECT_TRUE(first.withoutNodeTable.empty());

  const auto second = subscriptions.takeRecipients(3);
  EXPECT_TRUE(second.withNodeTable.empty());
  EXPECT_EQ(second.withoutNodeTable, (std::vector<int32_t>{5}));

  const auto third = subscriptions.takeRecipients(4);
  EXPECT_EQ(third.withNodeTable, (std::vector<int32_t>{5}));
  EXPECT_TRUE(third.withoutNodeTable.empty());
}

TEST(StatsSubscriptions, ResubscribingAfterAnUnsubscribeGetsTheTableAgain)
{
  net::StatsSubscriptions subscriptions;
  subscriptions.subscribe(1);
  static_cast<void>(subscriptions.takeRecipients(3));

  subscriptions.unsubscribe(1);
  EXPECT_FALSE(subscriptions.isSubscribed(1));
  EXPECT_TRUE(subscriptions.empty());

  subscriptions.subscribe(1);
  EXPECT_EQ(subscriptions.takeRecipients(3).withNodeTable, (std::vector<int32_t>{1}));
}

TEST(StatsSubscriptions, SubscribingTwiceIsOneSubscriberThatGetsTheTableAgain)
{
  net::StatsSubscriptions subscriptions;
  subscriptions.subscribe(1);
  static_cast<void>(subscriptions.takeRecipients(3));

  subscriptions.subscribe(1);
  const auto recipients = subscriptions.takeRecipients(3);

  EXPECT_EQ(recipients.withNodeTable, (std::vector<int32_t>{1}));
  EXPECT_TRUE(recipients.withoutNodeTable.empty());
}

TEST(StatsSubscriptions, ForgetRemovesTheSubscriberAndTheRefusalWarning)
{
  net::StatsSubscriptions subscriptions;
  subscriptions.subscribe(2);
  EXPECT_TRUE(subscriptions.warnRefusalOnce(2));
  EXPECT_FALSE(subscriptions.warnRefusalOnce(2));

  subscriptions.forget(2);

  EXPECT_TRUE(subscriptions.empty());
  EXPECT_TRUE(subscriptions.warnRefusalOnce(2));
}

TEST(StatsSubscriptions, RefusalWarningsAreIndependentPerConnection)
{
  net::StatsSubscriptions subscriptions;

  EXPECT_TRUE(subscriptions.warnRefusalOnce(1));
  EXPECT_TRUE(subscriptions.warnRefusalOnce(2));
  EXPECT_FALSE(subscriptions.warnRefusalOnce(1));
}

TEST(StatsSubscriptions, SubscribersInDifferentStatesSplitAndSort)
{
  net::StatsSubscriptions subscriptions;
  subscriptions.subscribe(9);
  subscriptions.subscribe(4);
  static_cast<void>(subscriptions.takeRecipients(7));

  subscriptions.subscribe(8);
  subscriptions.subscribe(1);
  const auto recipients = subscriptions.takeRecipients(7);

  EXPECT_EQ(recipients.withNodeTable, (std::vector<int32_t>{1, 8}));
  EXPECT_EQ(recipients.withoutNodeTable, (std::vector<int32_t>{4, 9}));
}
