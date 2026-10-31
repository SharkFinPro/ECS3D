#include <gtest/gtest.h>

#include "Replication.h"

#include <Protocol.h>
#include <cstdint>
#include <stdexcept>
#include <string>
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

TEST(InputStatePayload, TheViewBlockRoundTripsAfterTheMouseBlock)
{
  const replication::InputView view{ "0b0d0f6c-3f1c-4f3b-9d56-0d6f4b6a1c11", 12.5f, 99.0f, 640.0f, 360.0f };
  const auto message = replication::buildInputState(true, { 87 }, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 0b10u, view);

  const auto payload = replication::parseInputState(message);
  ASSERT_TRUE(payload.has_value());
  ASSERT_TRUE(payload->hasMouse);
  EXPECT_EQ(payload->buttons, 0b10u);
  ASSERT_TRUE(payload->view.has_value());
  EXPECT_EQ(payload->view->camera, view.camera);
  EXPECT_EQ(payload->view->cursorX, 12.5f);
  EXPECT_EQ(payload->view->cursorY, 99.0f);
  EXPECT_EQ(payload->view->width, 640.0f);
  EXPECT_EQ(payload->view->height, 360.0f);
}

TEST(InputStatePayload, AMessageWrittenWithoutTheViewBlockKeepsItsMouseAndHasNoView)
{
  // The bytes the format had before the view block: keys, then the mouse block, nothing after.
  net::Message older(net::MessageType::inputState);
  older.write<bool>(true);
  older.write<uint32_t>(1);
  older.write<int32_t>(65);
  for (const float value : { 10.0f, 20.0f, 1.0f, -1.0f, 0.5f })
  {
    older.write<float>(value);
  }
  older.write<uint8_t>(0b1u);

  const auto payload = replication::parseInputState(older);
  ASSERT_TRUE(payload.has_value());
  ASSERT_TRUE(payload->hasMouse);
  EXPECT_EQ(payload->mouseX, 10.0f);
  EXPECT_EQ(payload->buttons, 0b1u);
  EXPECT_FALSE(payload->view.has_value());

  // Positive control: the same builder given a view reports it.
  const auto withView = replication::parseInputState(
    replication::buildInputState(true, { 65 }, 10.0f, 20.0f, 1.0f, -1.0f, 0.5f, 0b1u, replication::InputView{}));
  ASSERT_TRUE(withView.has_value());
  EXPECT_TRUE(withView->view.has_value());
}

TEST(InputStatePayload, AnEmptyCameraStringRoundTripsAsAViewWithNoCamera)
{
  const replication::InputView view{ "", 5.0f, 6.0f, 100.0f, 50.0f };
  const auto payload = replication::parseInputState(
    replication::buildInputState(true, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, view));

  ASSERT_TRUE(payload.has_value());
  ASSERT_TRUE(payload->view.has_value());
  EXPECT_TRUE(payload->view->camera.empty());
  EXPECT_EQ(payload->view->cursorX, 5.0f);
  EXPECT_EQ(payload->view->height, 50.0f);
}

TEST(InputStatePayload, ATruncatedViewBlockIsTreatedAsAbsentWhileTheMouseBlockSurvives)
{
  const replication::InputView view{ "abc", 1.0f, 2.0f, 3.0f, 4.0f };
  const auto full = replication::buildInputState(true, {}, 7.0f, 8.0f, 0.0f, 0.0f, 0.0f, 0, view);
  const auto bytes = full.bytes();

  const net::Message cut(net::MessageType::inputState, bytes.first(bytes.size() - 1));

  const auto payload = replication::parseInputState(cut);
  ASSERT_TRUE(payload.has_value());
  ASSERT_TRUE(payload->hasMouse);
  EXPECT_EQ(payload->mouseX, 7.0f);
  EXPECT_FALSE(payload->view.has_value());

  // Positive control: the uncut message carries the view.
  ASSERT_TRUE(replication::parseInputState(full)->view.has_value());
}

TEST(InputStatePayload, ACameraStringPastTheMaxViewCameraBytesIsNoViewWhileTheLongestAllowedOneParses)
{
  static_assert(replication::maxViewCameraBytes == 64);

  const std::string longest(64, 'a');
  const auto accepted = replication::parseInputState(
    replication::buildInputState(true, {}, 7.0f, 8.0f, 0.0f, 0.0f, 0.0f, 0,
                                 replication::InputView{ longest, 1.0f, 2.0f, 3.0f, 4.0f }));
  ASSERT_TRUE(accepted.has_value());
  ASSERT_TRUE(accepted->view.has_value());
  EXPECT_EQ(accepted->view->camera, longest);

  const std::string huge(100000, 'a');
  const auto refused = replication::parseInputState(
    replication::buildInputState(true, {}, 7.0f, 8.0f, 0.0f, 0.0f, 0.0f, 0,
                                 replication::InputView{ huge, 1.0f, 2.0f, 3.0f, 4.0f }));
  ASSERT_TRUE(refused.has_value());
  EXPECT_TRUE(refused->hasMouse);
  EXPECT_EQ(refused->mouseX, 7.0f);
  EXPECT_FALSE(refused->view.has_value());

  const std::string justOver(65, 'a');
  const auto over = replication::parseInputState(
    replication::buildInputState(true, {}, 7.0f, 8.0f, 0.0f, 0.0f, 0.0f, 0,
                                 replication::InputView{ justOver, 1.0f, 2.0f, 3.0f, 4.0f }));
  ASSERT_TRUE(over.has_value());
  EXPECT_FALSE(over->view.has_value());
}

TEST(InputStatePayload, ACameraLengthClaimingMoreThanTheMessageHoldsIsNoViewWithoutThrowing)
{
  net::Message claim(net::MessageType::inputState);
  claim.write<bool>(true);
  claim.write<uint32_t>(0);
  for (const float value : { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f })
  {
    claim.write<float>(value);
  }
  claim.write<uint8_t>(0);
  claim.write<uint32_t>(0xFFFFFFFFu);
  for (int i = 0; i < 4; ++i)
  {
    claim.write<float>(1.0f);
  }

  const auto payload = replication::parseInputState(claim);
  ASSERT_TRUE(payload.has_value());
  EXPECT_TRUE(payload->hasMouse);
  EXPECT_FALSE(payload->view.has_value());
}
