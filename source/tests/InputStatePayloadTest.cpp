#include <gtest/gtest.h>

#include "Replication.h"

#include <Protocol.h>
#include <cstdint>
#include <stdexcept>
#include <vector>

// parseInputState/buildInputState carry ServerApp::handleInputState's parse of the client's per-frame
// input - keys pressed, focus and the optional mouse block - as free functions Replication already
// exposes for every other wire payload, so the bound on the key count and the mouse-block tolerance are
// covered here without booting the CLR that ServerApp's constructor otherwise requires.

TEST(InputStatePayload, RoundTripsEveryFieldIncludingAMovingMouse)
{
  const auto message = replication::buildInputState(false, { 42, -7 }, 12.5f, -3.25f, 1.0f, -2.0f, 0.5f, 0b101u);

  const auto payload = replication::parseInputState(message);
  ASSERT_TRUE(payload.has_value());

  EXPECT_FALSE(payload->focused);
  EXPECT_EQ(payload->keysPressed, (std::vector<int>{ 42, -7 }));
  ASSERT_TRUE(payload->hasMouse);
  EXPECT_EQ(payload->mouseX, 12.5f);
  EXPECT_EQ(payload->mouseY, -3.25f);
  EXPECT_EQ(payload->mouseDeltaX, 1.0f);
  EXPECT_EQ(payload->mouseDeltaY, -2.0f);
  EXPECT_EQ(payload->scrollY, 0.5f);
  EXPECT_EQ(payload->buttons, 0b101u);
}

TEST(InputStatePayload, AMessageWithNoMouseBlockAtAllParsesWithMouseAbsent)
{
  // A pre-mouse-block client: focused + key count, nothing after. Its absence must not be malformed - the
  // positive control below (a message with the same prefix, plus a mouse block appended) shows the same
  // bytes parse to hasMouse true once the block is actually there.
  net::Message withoutMouse(net::MessageType::inputState);
  withoutMouse.write<bool>(true);
  withoutMouse.write<uint32_t>(0);

  const auto payload = replication::parseInputState(withoutMouse);
  ASSERT_TRUE(payload.has_value());
  EXPECT_TRUE(payload->focused);
  EXPECT_TRUE(payload->keysPressed.empty());
  EXPECT_FALSE(payload->hasMouse);

  const auto withMouse = replication::buildInputState(true, {}, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 0);
  const auto withMousePayload = replication::parseInputState(withMouse);
  ASSERT_TRUE(withMousePayload.has_value());
  EXPECT_TRUE(withMousePayload->hasMouse);
}

TEST(InputStatePayload, AcceptsZeroKeys)
{
  const auto message = replication::buildInputState(true, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0);

  const auto payload = replication::parseInputState(message);
  ASSERT_TRUE(payload.has_value());

  EXPECT_TRUE(payload->keysPressed.empty());
  // Positive control: the same builder with actual keys produces a non-empty list, so the emptiness
  // above reflects what was asked for rather than every payload parsing to no keys.
  const auto withKeys = replication::parseInputState(replication::buildInputState(true, { 3 }, 0, 0, 0, 0, 0, 0));
  ASSERT_TRUE(withKeys.has_value());
  EXPECT_EQ(withKeys->keysPressed, (std::vector<int>{ 3 }));
}

TEST(InputStatePayload, AcceptsAKeyCountExactlyAtTheBoundButRefusesOneMore)
{
  // remaining() after the count is read is an exact byte budget; the bound compares the claimed count
  // against how many whole int32_t keys that budget could hold. One trailing key code (4 bytes) makes a
  // claimed count of 1 the boundary and 2 one past it.
  net::Message atBound(net::MessageType::inputState);
  atBound.write<bool>(true);
  atBound.write<uint32_t>(1);
  atBound.write<int32_t>(99);

  const auto payload = replication::parseInputState(atBound);
  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ(payload->keysPressed, (std::vector<int>{ 99 }));
  // Nothing left for the (optional) mouse block, so it stays absent rather than misread as one.
  EXPECT_FALSE(payload->hasMouse);

  net::Message overBound(net::MessageType::inputState);
  overBound.write<bool>(true);
  overBound.write<uint32_t>(2);
  overBound.write<int32_t>(99);

  EXPECT_FALSE(replication::parseInputState(overBound).has_value());
}

TEST(InputStatePayload, ATruncatedKeyListIsRefusedWhileTheSameBytesPlusTheMissingKeyAreAccepted)
{
  // Claims 2 keys but only one key's worth of bytes (4) follow - refused by the same bound check as the
  // oversized-count case above, since the byte budget cannot back the claimed count.
  net::Message truncated(net::MessageType::inputState);
  truncated.write<bool>(true);
  truncated.write<uint32_t>(2);
  truncated.write<int32_t>(7);

  EXPECT_FALSE(replication::parseInputState(truncated).has_value());

  // Positive control: the identical claimed count, this time backed by both keys' worth of bytes.
  net::Message complete(net::MessageType::inputState);
  complete.write<bool>(true);
  complete.write<uint32_t>(2);
  complete.write<int32_t>(7);
  complete.write<int32_t>(8);

  const auto payload = replication::parseInputState(complete);
  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ(payload->keysPressed, (std::vector<int>{ 7, 8 }));
}

TEST(InputStatePayload, AnEmptyMessageThrowsWhileOneCarryingTheLeadingBoolParses)
{
  const net::Message empty(net::MessageType::inputState);
  EXPECT_THROW(static_cast<void>(replication::parseInputState(empty)), std::runtime_error);

  // Positive control: the same message with just its leading bool (and a zero key count) present parses
  // cleanly, so the throw above is specifically about the missing byte, not something else in the setup.
  net::Message minimal(net::MessageType::inputState);
  minimal.write<bool>(true);
  minimal.write<uint32_t>(0);

  EXPECT_TRUE(replication::parseInputState(minimal).has_value());
}

TEST(InputStatePayload, ATruncatedMouseBlockIsTreatedAsAbsentWhileAFullOneParsesAsPresent)
{
  // Fewer bytes than the fixed mouse payload needs is tolerated the same way a client that predates the
  // mouse block entirely is tolerated, rather than being treated as malformed.
  net::Message truncatedMouse(net::MessageType::inputState);
  truncatedMouse.write<bool>(true);
  truncatedMouse.write<uint32_t>(0);
  truncatedMouse.write<float>(1.0f);
  truncatedMouse.write<float>(2.0f); // 8 of the 21 bytes the full mouse block needs

  const auto payload = replication::parseInputState(truncatedMouse);
  ASSERT_TRUE(payload.has_value());
  EXPECT_FALSE(payload->hasMouse);

  // Positive control: the identical prefix, this time with a complete mouse block.
  const auto complete = replication::buildInputState(true, {}, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 0b1u);
  const auto completePayload = replication::parseInputState(complete);
  ASSERT_TRUE(completePayload.has_value());
  EXPECT_TRUE(completePayload->hasMouse);
  EXPECT_EQ(completePayload->mouseX, 1.0f);
}

TEST(InputStatePayload, TrailingBytesPastAWellFormedMouseBlockAreIgnoredRatherThanRefused)
{
  // Positive control is RoundTripsEveryFieldIncludingAMovingMouse above (the identical well-formed shape
  // with no extra bytes parses to the same fields); appending garbage after a complete mouse block must
  // not disturb that, since nothing past the fixed mouse payload is ever read.
  auto message = replication::buildInputState(true, { 5 }, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 0b1u);
  message.write<uint32_t>(0xDEADBEEFu);

  const auto payload = replication::parseInputState(message);
  ASSERT_TRUE(payload.has_value());

  EXPECT_EQ(payload->keysPressed, (std::vector<int>{ 5 }));
  ASSERT_TRUE(payload->hasMouse);
  EXPECT_EQ(payload->buttons, 0b1u);
}
