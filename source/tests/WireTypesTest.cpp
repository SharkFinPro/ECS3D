#include <gtest/gtest.h>

#include "WireTypes.h"
#include "TestPrinters.h"

#include <Protocol.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <uuid.h>

namespace {
  template <typename T>
  concept GoesOnTheWire = requires (net::Message message, const T& value) { message.write(value); };
}

// The trait's specializations live in data, so the assertions about them live in the one test file that
// includes them - a translation unit that queried the trait without WireTypes.h would silently evaluate
// the primary template and disagree with the rest of the program.
TEST(WireTypes, TheOptedInAggregatesAreAcceptedWhole)
{
  static_assert(GoesOnTheWire<glm::vec3>);
  static_assert(GoesOnTheWire<uuids::uuid>);
}

TEST(WireTypes, TheirLayoutIsWhatTheOptInClaims)
{
  // The claim behind the opt-in: no padding to leak, and a stride both peers agree on.
  static_assert(sizeof(glm::vec3) == 12);
  static_assert(sizeof(uuids::uuid) == 16);

  net::Message message(net::MessageType::stateDelta);
  message.write(glm::vec3(1.0f, 2.0f, 3.0f));

  EXPECT_EQ(message.size(), 12u);

  net::MessageReader reader(message);
  EXPECT_EQ(reader.read<glm::vec3>(), glm::vec3(1.0f, 2.0f, 3.0f));
  EXPECT_EQ(reader.remaining(), 0u);
}

TEST(WireTypes, NeighboringAggregatesAreNotOptedInByAccident)
{
  // All trivially copyable, all one specialization away from the wire, none of them claimed.
  static_assert(!GoesOnTheWire<glm::vec2>);
  static_assert(!GoesOnTheWire<glm::mat4>);
  static_assert(!GoesOnTheWire<glm::quat>);
}
