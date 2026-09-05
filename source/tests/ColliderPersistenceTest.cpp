#include <gtest/gtest.h>

#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/SphereCollider.h"

#include <Protocol.h>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <memory>

namespace {
  template <typename T>
  std::shared_ptr<T> throughJson(const std::shared_ptr<T>& source)
  {
    auto loaded = std::make_shared<T>();
    loaded->loadFromJSON(source->serialize());

    return loaded;
  }

  template <typename T>
  std::shared_ptr<T> throughTheWire(const std::shared_ptr<T>& source)
  {
    // The framing is what matters here, not the message type: pack writes the component alone.
    net::Message message(net::MessageType::undefined);
    source->pack(message);

    net::MessageReader reader(message);
    // pack writes the type discriminator first; unpack expects the reader positioned after it.
    static_cast<void>(reader.read<ComponentType>());

    auto loaded = std::make_shared<T>();
    loaded->unpack(reader);

    return loaded;
  }
}

TEST(ColliderPersistence, ABoxKeepsItsGizmoFlagThroughBothPaths)
{
  const auto collider = std::make_shared<BoxCollider>();
  collider->setRenderCollider(true);

  // The save file and the wire have to describe the same collider; the flag rode one and not the other.
  EXPECT_TRUE(throughJson(collider)->getRenderCollider());
  EXPECT_TRUE(throughTheWire(collider)->getRenderCollider());
}

TEST(ColliderPersistence, ASphereKeepsItsGizmoFlagThroughBothPaths)
{
  const auto collider = std::make_shared<SphereCollider>();
  collider->setRenderCollider(true);

  EXPECT_TRUE(throughJson(collider)->getRenderCollider());
  EXPECT_TRUE(throughTheWire(collider)->getRenderCollider());
}

TEST(ColliderPersistence, ABoxRoundTripsItsLayerAndMask)
{
  const auto collider = std::make_shared<BoxCollider>();
  collider->setLayer(7);
  collider->setMask(0xF0u);
  collider->setIsTrigger(true);

  const auto fromJson = throughJson(collider);
  EXPECT_EQ(fromJson->getLayer(), 7u);
  EXPECT_EQ(fromJson->getMask(), 0xF0u);
  EXPECT_TRUE(fromJson->isTrigger());

  const auto fromWire = throughTheWire(collider);
  EXPECT_EQ(fromWire->getLayer(), 7u);
  EXPECT_EQ(fromWire->getMask(), 0xF0u);
  EXPECT_TRUE(fromWire->isTrigger());
}

TEST(ColliderPersistence, AnOutOfRangeLayerIsClampedOnLoad)
{
  const auto collider = std::make_shared<BoxCollider>();

  auto saved = collider->serialize();
  saved["layer"] = 99u;

  // The layer indexes a 32-bit mask, so a hand-edited or corrupt file must not shift out of range.
  const auto loaded = std::make_shared<BoxCollider>();
  loaded->loadFromJSON(saved);

  EXPECT_EQ(loaded->getLayer(), 31u);
}

TEST(ColliderPersistence, AnOutOfRangeLayerIsClampedOffTheWire)
{
  net::Message message(net::MessageType::undefined);
  message.write(ComponentType::SubComponentType_sphereCollider);
  message.write(false);
  message.write(glm::vec3(0));
  message.write(1.0f);
  message.write(false);
  message.write(uint32_t{ 99 });
  message.write(uint32_t{ 0xFFFFFFFFu });

  net::MessageReader reader(message);
  static_cast<void>(reader.read<ComponentType>());

  const auto collider = std::make_shared<SphereCollider>();
  collider->unpack(reader);

  EXPECT_EQ(collider->getLayer(), 31u);
}
