#include <gtest/gtest.h>

#include <Protocol.h>
#include <ServerLog.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {
  // Trivially copyable, and padded: the bool leaves three trailing bytes holding whatever the stack last
  // put there, and sending it would put them in front of every peer. The concept has to refuse it.
  struct PaddedFields {
    int32_t first;
    float second;
    bool third;
  };

  // Trivially copyable too, and holding an address that means nothing to the peer that receives it.
  struct HoldsAPointer {
    const int* borrowed;
  };

  template <typename T>
  concept GoesOnTheWire = requires (net::Message message, const T& value) { message.write(value); };
}

TEST(ProtocolFraming, CarriesItsTypeAndStartsEmpty)
{
  const net::Message message(net::MessageType::snapshot);

  EXPECT_EQ(message.getType(), net::MessageType::snapshot);
  EXPECT_EQ(message.size(), 0u);

  const net::Message defaulted;

  EXPECT_EQ(defaulted.getType(), net::MessageType::undefined);
  EXPECT_EQ(defaulted.size(), 0u);
}

TEST(ProtocolFraming, RoundTripsEveryFieldWidth)
{
  net::Message message(net::MessageType::stateDelta);
  message.write<uint8_t>(0xABu)
         .write<uint16_t>(0xBEEFu)
         .write<uint32_t>(0xDEADBEEFu)
         .write<uint64_t>(0x0123456789ABCDEFull)
         .write<int32_t>(-42)
         .write<float>(1.5f)
         .write<double>(-2.25)
         .write<bool>(true)
         .write(net::MessageType::inputState);

  net::MessageReader reader(message);

  EXPECT_EQ(reader.read<uint8_t>(), 0xABu);
  EXPECT_EQ(reader.read<uint16_t>(), 0xBEEFu);
  EXPECT_EQ(reader.read<uint32_t>(), 0xDEADBEEFu);
  EXPECT_EQ(reader.read<uint64_t>(), 0x0123456789ABCDEFull);
  EXPECT_EQ(reader.read<int32_t>(), -42);
  EXPECT_EQ(reader.read<float>(), 1.5f);
  EXPECT_EQ(reader.read<double>(), -2.25);
  EXPECT_EQ(reader.read<bool>(), true);
  EXPECT_EQ(reader.read<net::MessageType>(), net::MessageType::inputState);

  EXPECT_EQ(reader.remaining(), 0u);
}

TEST(ProtocolFraming, RefusesTypesThatAreTriviallyCopyableButNotSafeToSend)
{
  static_assert(std::is_trivially_copyable_v<PaddedFields> && std::is_trivially_copyable_v<HoldsAPointer>,
                "Both would have satisfied a plain trivially-copyable constraint.");

  static_assert(offsetof(PaddedFields, first) == 0 && offsetof(PaddedFields, second) == sizeof(int32_t) &&
                offsetof(PaddedFields, third) == sizeof(int32_t) + sizeof(float) &&
                sizeof(PaddedFields) > offsetof(PaddedFields, third) + sizeof(bool),
                "The bool has to leave trailing padding bytes for the test to mean anything.");
  static_assert(sizeof(HoldsAPointer::borrowed) == sizeof(void*));

  static_assert(GoesOnTheWire<uint32_t>);
  static_assert(GoesOnTheWire<float>);
  static_assert(GoesOnTheWire<net::MessageType>);

  static_assert(!GoesOnTheWire<PaddedFields>);
  static_assert(!GoesOnTheWire<HoldsAPointer>);
  static_assert(!GoesOnTheWire<const int*>);

  // 16 bytes of which only 10 carry value on the x86-64 System V ABI, and 8 on MSVC - the padded scalar.
  static_assert(!GoesOnTheWire<long double>);
}

TEST(ProtocolFraming, ACvQualifiedBoolIsStillNarrowedRatherThanReinterpreted)
{
  net::Message message(net::MessageType::inputState);
  message.write<uint8_t>(0x2Au);

  net::MessageReader reader(message);

  // Spelled with a qualifier the bool branch keys on remove_cv_t, or the byte would be bit_cast into a
  // bool holding a value no bool has - the undefined case the plain spelling already avoids.
  EXPECT_TRUE(reader.read<const bool>());
}

TEST(ProtocolFraming, ABoolIsWrittenAsOneZeroOrOneByte)
{
  net::Message message(net::MessageType::stateDelta);
  message.write<bool>(true).write<bool>(false);

  // A format lock rather than a test of the narrowing: sizeof(bool) is already 1 everywhere this builds,
  // so the byte written is what a peer of any vintage expects to read.
  ASSERT_EQ(message.size(), 2u);
  EXPECT_EQ(message.bytes()[0], 1u);
  EXPECT_EQ(message.bytes()[1], 0u);
}

TEST(ProtocolFraming, AnyNonZeroByteReadsBackAsTrue)
{
  net::Message message(net::MessageType::inputState);
  message.write<uint8_t>(0x2Au).write<uint8_t>(0xFFu).write<uint8_t>(0u);

  net::MessageReader reader(message);

  // A byte off the network is not a bool. Reading one as a bool has to narrow rather than reinterpret,
  // or a value outside 0/1 is a bool with no value - which compilers optimize on the assumption it
  // cannot happen, taking both sides of the same branch.
  EXPECT_TRUE(reader.read<bool>());
  EXPECT_TRUE(reader.read<bool>());
  EXPECT_FALSE(reader.read<bool>());
}

TEST(ProtocolFraming, WritesExactlyTheSizeOfWhatItIsGiven)
{
  net::Message message(net::MessageType::stateDelta);

  message.write<uint32_t>(1);
  EXPECT_EQ(message.size(), sizeof(uint32_t));

  message.write<uint8_t>(1);
  EXPECT_EQ(message.size(), sizeof(uint32_t) + sizeof(uint8_t));

  EXPECT_EQ(message.bytes().size(), message.size());
}

TEST(ProtocolFraming, EncodesInNativeByteOrder)
{
  net::Message message(net::MessageType::stateDelta);
  message.write<uint32_t>(0x01020304u);

  const auto bytes = message.bytes();
  ASSERT_EQ(bytes.size(), 4u);

  // The primitives bit_cast straight into the payload, so the wire carries host byte order with no
  // normalization at all - two peers of different endianness would disagree about every integer.
  static_assert(std::endian::native == std::endian::little,
                "The wire carries host byte order; a big-endian port needs a byte-order contract first.");

  EXPECT_EQ(bytes[0], 0x04u);
  EXPECT_EQ(bytes[3], 0x01u);
}

TEST(ProtocolFraming, RoundTripsStringsIncludingAwkwardOnes)
{
  const std::string embeddedNull("be\0fore", 7);

  net::Message message(net::MessageType::sceneEdit);
  message.writeString("plain");
  message.writeString("");
  message.writeString(embeddedNull);
  message.writeString("\xC3\xA9\xF0\x9F\x92\xA9");

  net::MessageReader reader(message);

  EXPECT_EQ(reader.readString(), "plain");
  EXPECT_EQ(reader.readString(), "");
  EXPECT_EQ(reader.readString(), embeddedNull);
  EXPECT_EQ(reader.readString(), "\xC3\xA9\xF0\x9F\x92\xA9");
  EXPECT_EQ(reader.remaining(), 0u);
}

TEST(ProtocolFraming, AStringIsLengthPrefixed)
{
  net::Message message(net::MessageType::sceneEdit);
  message.writeString("abc");

  EXPECT_EQ(message.size(), sizeof(uint32_t) + 3u);

  net::MessageReader reader(message);
  EXPECT_EQ(reader.read<uint32_t>(), 3u);
}

TEST(ProtocolFraming, InterleavesStringsAndScalarsInOrder)
{
  net::Message message(net::MessageType::sceneEdit);
  message.write<uint32_t>(7);
  message.writeString("middle");
  message.write<float>(0.5f);

  net::MessageReader reader(message);

  EXPECT_EQ(reader.read<uint32_t>(), 7u);
  EXPECT_EQ(reader.readString(), "middle");
  EXPECT_EQ(reader.read<float>(), 0.5f);
}

TEST(ProtocolFraming, RemainingTracksWhatIsLeft)
{
  net::Message message(net::MessageType::stateDelta);
  message.write<uint32_t>(1).write<uint16_t>(2);

  net::MessageReader reader(message);
  EXPECT_EQ(reader.remaining(), 6u);

  static_cast<void>(reader.read<uint32_t>());
  EXPECT_EQ(reader.remaining(), 2u);

  static_cast<void>(reader.read<uint16_t>());
  EXPECT_EQ(reader.remaining(), 0u);
}

TEST(ProtocolFraming, ReadingPastTheEndThrows)
{
  const net::Message empty(net::MessageType::stateDelta);
  net::MessageReader emptyReader(empty);

  EXPECT_THROW(static_cast<void>(emptyReader.read<uint8_t>()), std::runtime_error);

  net::Message message(net::MessageType::stateDelta);
  message.write<uint16_t>(1);

  net::MessageReader reader(message);

  // Enough bytes for the value written, not enough for the one asked for.
  EXPECT_THROW(static_cast<void>(reader.read<uint32_t>()), std::runtime_error);
}

TEST(ProtocolFraming, ReadsRightUpToTheLastByte)
{
  net::Message message(net::MessageType::sceneEdit);
  message.write<uint32_t>(3);
  message.write<uint8_t>('a');
  message.write<uint8_t>('b');
  message.write<uint8_t>('c');

  net::MessageReader reader(message);

  // The bounds checks are strict comparisons, so exactly-enough has to succeed rather than trip them.
  EXPECT_EQ(reader.readString(), "abc");
  EXPECT_EQ(reader.remaining(), 0u);
}

TEST(ProtocolFraming, ReadsAValueThatExactlyFillsThePayload)
{
  net::Message message(net::MessageType::stateDelta);
  message.write<uint32_t>(0x01020304u);

  net::MessageReader reader(message);

  EXPECT_EQ(reader.read<uint32_t>(), 0x01020304u);
  EXPECT_EQ(reader.remaining(), 0u);
}

TEST(ProtocolFraming, AStringLengthLongerThanThePayloadThrows)
{
  net::Message message(net::MessageType::sceneEdit);
  message.write<uint32_t>(64);
  message.write<uint8_t>('x');

  net::MessageReader reader(message);

  // The malformed case that matters: a length prefix off the network must not be trusted to read past
  // the end of the buffer.
  EXPECT_THROW(static_cast<void>(reader.readString()), std::runtime_error);
}

TEST(ProtocolFraming, AStringLengthLongerThanThePayloadLeavesTheReaderWhereItWas)
{
  net::Message message(net::MessageType::sceneEdit);
  message.write<uint32_t>(64);
  message.write<uint8_t>('x');

  net::MessageReader reader(message);

  EXPECT_THROW(static_cast<void>(reader.readString()), std::runtime_error);

  // A failed readString must not have moved the offset: the positive control is reading the same four
  // bytes back as the uint32 length prefix they actually are, not as a discarded partial string read.
  EXPECT_EQ(reader.remaining(), message.size());
  EXPECT_EQ(reader.read<uint32_t>(), 64u);
}

TEST(ProtocolFraming, AnAbsurdStringLengthThrowsRatherThanOverreading)
{
  net::Message message(net::MessageType::sceneEdit);
  message.write(std::numeric_limits<uint32_t>::max());

  net::MessageReader reader(message);

  EXPECT_THROW(static_cast<void>(reader.readString()), std::runtime_error);
}

TEST(ProtocolFraming, ATruncatedStringPrefixThrows)
{
  net::Message message(net::MessageType::sceneEdit);
  message.write<uint16_t>(3);

  net::MessageReader reader(message);

  EXPECT_THROW(static_cast<void>(reader.readString()), std::runtime_error);
}

TEST(ProtocolFraming, IsMutationMessageMatchesTheDocumentedEditorMutationPath)
{
  EXPECT_TRUE(net::isMutationMessage(net::MessageType::editComponent));
  EXPECT_TRUE(net::isMutationMessage(net::MessageType::sceneEdit));
  EXPECT_TRUE(net::isMutationMessage(net::MessageType::sceneControl));
  EXPECT_TRUE(net::isMutationMessage(net::MessageType::loadProject));
  EXPECT_TRUE(net::isMutationMessage(net::MessageType::addAsset));
  EXPECT_TRUE(net::isMutationMessage(net::MessageType::renameAsset));
  EXPECT_TRUE(net::isMutationMessage(net::MessageType::removeAsset));
}

TEST(ProtocolFraming, IsMutationMessageIsFalseForEverythingElse)
{
  // The positive control above proves the switch fires at all; this is every remaining MessageType, so a
  // type added to one enum without the other fails one of the two tests instead of silently drifting.
  EXPECT_FALSE(net::isMutationMessage(net::MessageType::undefined));
  EXPECT_FALSE(net::isMutationMessage(net::MessageType::join));
  EXPECT_FALSE(net::isMutationMessage(net::MessageType::snapshot));
  EXPECT_FALSE(net::isMutationMessage(net::MessageType::stateDelta));
  EXPECT_FALSE(net::isMutationMessage(net::MessageType::inputState));
  EXPECT_FALSE(net::isMutationMessage(net::MessageType::editStatus));
  EXPECT_FALSE(net::isMutationMessage(net::MessageType::sceneStatus));
  EXPECT_FALSE(net::isMutationMessage(net::MessageType::objectSpawned));
  EXPECT_FALSE(net::isMutationMessage(net::MessageType::objectDestroyed));
  EXPECT_FALSE(net::isMutationMessage(net::MessageType::playerSlot));
  EXPECT_FALSE(net::isMutationMessage(net::MessageType::serverLog));
}

TEST(ProtocolFraming, ConstructingFromABufferFramesItExactlyAsTheWritePathWould)
{
  net::Message written(net::MessageType::snapshot);
  written.write<uint32_t>(0xDEADBEEFu);
  written.writeString("payload");
  written.write<float>(0.25f);

  const std::vector<uint8_t> received(written.bytes().begin(), written.bytes().end());

  const net::Message message(net::MessageType::snapshot, received);

  EXPECT_EQ(message.getType(), net::MessageType::snapshot);
  ASSERT_EQ(message.size(), received.size());
  EXPECT_TRUE(std::equal(message.bytes().begin(), message.bytes().end(), received.begin()));

  // The inbound path has to hand the reader the same bytes the write path produced, or a snapshot that
  // survived the wire would be read back at the wrong offsets.
  net::MessageReader reader(message);

  EXPECT_EQ(reader.read<uint32_t>(), 0xDEADBEEFu);
  EXPECT_EQ(reader.readString(), "payload");
  EXPECT_EQ(reader.read<float>(), 0.25f);
  EXPECT_EQ(reader.remaining(), 0u);
}

TEST(ProtocolFraming, AppendedBytesSitBetweenTheValuesWrittenAroundThem)
{
  const std::string dumped("{\"k\":1}");
  const std::span raw(reinterpret_cast<const uint8_t*>(dumped.data()), dumped.size());

  net::Message message(net::MessageType::sceneEdit);
  message.write<uint32_t>(static_cast<uint32_t>(dumped.size()));
  message.appendBytes(raw);
  message.write<int32_t>(-7);

  net::MessageReader reader(message);

  const auto bodySize = reader.read<uint32_t>();
  ASSERT_EQ(bodySize, dumped.size());

  std::string body;
  for (uint32_t i = 0; i < bodySize; ++i)
  {
    body.push_back(static_cast<char>(reader.read<uint8_t>()));
  }

  EXPECT_EQ(body, dumped);
  EXPECT_EQ(reader.read<int32_t>(), -7);
  EXPECT_EQ(reader.remaining(), 0u);
}

TEST(ProtocolFraming, AnEmptyBufferConstructsAnEmptyMessage)
{
  // The editor's join carries no payload and the server keys on that, so an empty span has to make an
  // ordinary empty message rather than anything the reader trips over.
  const net::Message message(net::MessageType::join, std::span<const uint8_t>());

  EXPECT_EQ(message.getType(), net::MessageType::join);
  EXPECT_EQ(message.size(), 0u);

  net::MessageReader reader(message);
  EXPECT_EQ(reader.remaining(), 0u);
}

TEST(ProtocolFraming, AMessageFromABufferDoesNotAliasIt)
{
  std::vector<uint8_t> received{1u, 2u, 3u, 4u};

  const net::Message message(net::MessageType::stateDelta, received);

  received[0] = 0xFFu;
  received.clear();

  // The transport owns the buffer only for the duration of the callback; the message it produced has to
  // keep its own copy or the inbox would be reading freed memory on the tick thread.
  ASSERT_EQ(message.size(), 4u);
  EXPECT_EQ(message.bytes()[0], 1u);
  EXPECT_EQ(message.bytes()[3], 4u);
}

TEST(ProtocolFraming, ATruncatedStringPrefixLeavesTheReaderWhereItWas)
{
  net::Message message(net::MessageType::sceneEdit);
  message.write<uint16_t>(3);

  net::MessageReader reader(message);

  EXPECT_THROW(static_cast<void>(reader.readString()), std::runtime_error);

  // Same positive control as above, for the shorter-than-the-prefix-itself case: the two bytes read
  // back as the uint16 that was actually written there.
  EXPECT_EQ(reader.remaining(), message.size());
  EXPECT_EQ(reader.read<uint16_t>(), 3u);
}

TEST(ProtocolFraming, AWellFormedStringFollowedByAnIntStillReadsBothCorrectly)
{
  net::Message message(net::MessageType::sceneEdit);
  message.writeString("abc");
  message.write<int32_t>(42);

  net::MessageReader reader(message);

  EXPECT_EQ(reader.readString(), "abc");
  EXPECT_EQ(reader.read<int32_t>(), 42);
  EXPECT_EQ(reader.remaining(), 0u);
}

TEST(ServerLogMessage, RoundTripsEveryFieldOfEachEntry)
{
  const auto first = std::chrono::system_clock::now() - std::chrono::seconds(5);
  const auto second = std::chrono::system_clock::now();

  const std::vector<LogEntry> entries{
    LogEntry{ first, LogLevel::warn, LogCategory::physics, "a bounce went wrong" },
    LogEntry{ second, LogLevel::error, LogCategory::script, "NullReferenceException in Player.cs" },
  };

  const auto message = net::packServerLog(entries, 0);
  EXPECT_EQ(message.getType(), net::MessageType::serverLog);

  const auto batch = net::unpackServerLog(message);

  EXPECT_EQ(batch.dropped, 0u);
  ASSERT_EQ(batch.entries.size(), 2u);

  EXPECT_EQ(batch.entries[0].level, LogLevel::warn);
  EXPECT_EQ(batch.entries[0].category, LogCategory::physics);
  EXPECT_EQ(batch.entries[0].message, "a bounce went wrong");
  // Millisecond precision on the wire, not exact time_point equality.
  EXPECT_LT(std::chrono::abs(batch.entries[0].time - first), std::chrono::milliseconds(1));

  EXPECT_EQ(batch.entries[1].level, LogLevel::error);
  EXPECT_EQ(batch.entries[1].category, LogCategory::script);
  EXPECT_EQ(batch.entries[1].message, "NullReferenceException in Player.cs");
  EXPECT_LT(std::chrono::abs(batch.entries[1].time - second), std::chrono::milliseconds(1));
}

TEST(ServerLogMessage, CarriesTheDroppedCountAlongsideAnEmptyBatch)
{
  // The forwarder sends a batch for a drop count alone, with no entries, so an editor still learns that
  // history was lost even on a tick where nothing new came in to replace it.
  const auto message = net::packServerLog({}, 7);

  const auto batch = net::unpackServerLog(message);

  EXPECT_EQ(batch.dropped, 7u);
  EXPECT_TRUE(batch.entries.empty());
}

TEST(ServerLogMessage, AnEntryCountPastWhatThePayloadHoldsThrowsRatherThanOverreading)
{
  // A hand-built message claiming far more entries than its (short) payload can actually hold - the same
  // malformed-count shape handleInputState guards against, here for the network's least trusted producer:
  // a batch this editor did not build itself.
  net::Message message(net::MessageType::serverLog);
  message.write<uint64_t>(0);
  message.write<uint32_t>(1000000);

  EXPECT_THROW(static_cast<void>(net::unpackServerLog(message)), std::runtime_error);
}
