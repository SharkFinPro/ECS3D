#include <gtest/gtest.h>

#include "Replication.h"
#include "scenes/SceneManager.h"
#include "WireTypes.h"

#include <Protocol.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace {
  net::Message truncated(const net::Message& message, const std::size_t length)
  {
    const auto bytes = message.bytes();
    return net::Message(message.getType(), std::span<const uint8_t>(bytes.data(), length));
  }
}

TEST(StatusPayload, JoinWithoutNonceParsesToNoNonce)
{
  const auto message = replication::buildJoin(std::nullopt);

  EXPECT_EQ(message.getType(), net::MessageType::join);
  EXPECT_EQ(message.size(), 0u);
  EXPECT_FALSE(replication::parseJoinNonce(message).has_value());
}

TEST(StatusPayload, JoinNonceRoundTripsIncludingExtremes)
{
  for (const uint64_t nonce : { uint64_t{0}, uint64_t{1}, std::numeric_limits<uint64_t>::max() })
  {
    const auto message = replication::buildJoin(nonce);

    EXPECT_EQ(message.getType(), net::MessageType::join);
    const auto parsed = replication::parseJoinNonce(message);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, nonce);
  }
}

TEST(StatusPayload, JoinWithPartialNonceParsesToNoNonce)
{
  const auto full = replication::buildJoin(std::numeric_limits<uint64_t>::max());
  ASSERT_TRUE(replication::parseJoinNonce(full).has_value());

  for (std::size_t length = 0; length < full.size(); ++length)
  {
    EXPECT_FALSE(replication::parseJoinNonce(truncated(full, length)).has_value()) << "length " << length;
  }
}

TEST(StatusPayload, PlayerSlotRoundTripsIncludingExtremes)
{
  struct Case {
    uint64_t nonce;
    int32_t slot;
  };

  const Case cases[] = {
    { 0, 0 },
    { std::numeric_limits<uint64_t>::max(), 0 },
    { 12345, 7 },
    { 1, std::numeric_limits<int32_t>::max() },
    { 2, -1 }
  };

  for (const auto& [nonce, slot] : cases)
  {
    const auto message = replication::buildPlayerSlot(nonce, slot);

    EXPECT_EQ(message.getType(), net::MessageType::playerSlot);
    const auto parsed = replication::parsePlayerSlot(message);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->nonce, nonce);
    EXPECT_EQ(parsed->slot, slot);
  }
}

TEST(StatusPayload, PlayerSlotLayoutIsNonceThenSlot)
{
  const auto message = replication::buildPlayerSlot(9, 4);

  net::MessageReader reader(message);
  EXPECT_EQ(reader.read<uint64_t>(), 9u);
  EXPECT_EQ(reader.read<int32_t>(), 4);
  EXPECT_EQ(reader.remaining(), 0u);
}

TEST(StatusPayload, EveryTruncatedPlayerSlotParsesToNothing)
{
  const auto full = replication::buildPlayerSlot(std::numeric_limits<uint64_t>::max(), 3);
  ASSERT_TRUE(replication::parsePlayerSlot(full).has_value());

  for (std::size_t length = 0; length < full.size(); ++length)
  {
    EXPECT_FALSE(replication::parsePlayerSlot(truncated(full, length)).has_value()) << "length " << length;
  }
}

TEST(StatusPayload, SlotForNonceKeepsOnlyAMatchingReply)
{
  const replication::PlayerSlotReply reply{ 42, 5 };

  const auto mine = replication::slotForNonce(reply, 42);
  ASSERT_TRUE(mine.has_value());
  EXPECT_EQ(*mine, 5);

  EXPECT_FALSE(replication::slotForNonce(reply, 43).has_value());
}

TEST(StatusPayload, SlotZeroIsAMatchNotAnAbsence)
{
  const auto slot = replication::slotForNonce(replication::PlayerSlotReply{ 0, 0 }, 0);

  ASSERT_TRUE(slot.has_value());
  EXPECT_EQ(*slot, 0);
}

TEST(StatusPayload, SceneStatusRoundTripsEveryValue)
{
  for (const auto status : { SceneStatus::running, SceneStatus::stopped, SceneStatus::paused })
  {
    const auto message = replication::buildSceneStatus(status);

    EXPECT_EQ(message.getType(), net::MessageType::sceneStatus);
    EXPECT_EQ(message.size(), 1u);
    const auto parsed = replication::parseSceneStatus(message);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, status);
  }
}

TEST(StatusPayload, SceneStatusRejectsAnUnknownValue)
{
  const auto valid = replication::buildSceneStatus(SceneStatus::paused);
  ASSERT_TRUE(replication::parseSceneStatus(valid).has_value());

  net::Message bogus(net::MessageType::sceneStatus);
  bogus.write(uint8_t{200});
  EXPECT_FALSE(replication::parseSceneStatus(bogus).has_value());
}

TEST(StatusPayload, EveryTruncatedSceneStatusParsesToNothing)
{
  const auto full = replication::buildSceneStatus(SceneStatus::running);
  ASSERT_TRUE(replication::parseSceneStatus(full).has_value());

  for (std::size_t length = 0; length < full.size(); ++length)
  {
    EXPECT_FALSE(replication::parseSceneStatus(truncated(full, length)).has_value()) << "length " << length;
  }
}

TEST(StatusPayload, EditStatusRoundTripsBothValues)
{
  for (const bool editable : { false, true })
  {
    const auto message = replication::buildEditStatus(editable);

    EXPECT_EQ(message.getType(), net::MessageType::editStatus);
    const auto parsed = replication::parseEditStatus(message);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, editable);
  }
}

TEST(StatusPayload, EveryTruncatedEditStatusParsesToNothing)
{
  const auto full = replication::buildEditStatus(true);
  ASSERT_TRUE(replication::parseEditStatus(full).has_value());

  for (std::size_t length = 0; length < full.size(); ++length)
  {
    EXPECT_FALSE(replication::parseEditStatus(truncated(full, length)).has_value()) << "length " << length;
  }
}
