#include <gtest/gtest.h>

#include <Protocol.h>

#include <bit>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
  // Padded on purpose: the primitives bit_cast whole objects, so a type with padding is worth carrying
  // through. Value-initialized at the call site so the padding is deterministic rather than whatever was
  // on the stack.
  struct PaddedFields {
    int32_t first;
    float second;
    bool third;

    bool operator==(const PaddedFields&) const = default;
  };

  PaddedFields makePaddedFields()
  {
    PaddedFields fields{};
    fields.first = -7;
    fields.second = 0.125f;
    fields.third = true;

    return fields;
  }
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
         .write(net::MessageType::inputState)
         .write(makePaddedFields());

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
  EXPECT_EQ(reader.read<PaddedFields>(), makePaddedFields());

  EXPECT_EQ(reader.remaining(), 0u);
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
