#include <gtest/gtest.h>

#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/Collider.h"
#include "objects/components/collisions/ConvexMeshCollider.h"
#include "objects/components/collisions/SphereCollider.h"
#include "WireTypes.h"

#include <Protocol.h>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <initializer_list>
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

TEST(ColliderPersistence, ABoxClearsItsGizmoFlagThroughBothPaths)
{
  const auto collider = std::make_shared<BoxCollider>();
  collider->setRenderCollider(false);

  // The targets start flagged so a load that skipped the field would leave true behind.
  const auto json = std::make_shared<BoxCollider>();
  json->setRenderCollider(true);
  json->loadFromJSON(collider->serialize());
  EXPECT_FALSE(json->getRenderCollider());

  net::Message message(net::MessageType::undefined);
  collider->pack(message);
  net::MessageReader reader(message);
  static_cast<void>(reader.read<ComponentType>());

  const auto wire = std::make_shared<BoxCollider>();
  wire->setRenderCollider(true);
  wire->unpack(reader);
  EXPECT_FALSE(wire->getRenderCollider());
}

TEST(ColliderPersistence, ASphereClearsItsGizmoFlagThroughBothPaths)
{
  const auto collider = std::make_shared<SphereCollider>();
  collider->setRenderCollider(false);

  // The targets start flagged so a load that skipped the field would leave true behind.
  const auto json = std::make_shared<SphereCollider>();
  json->setRenderCollider(true);
  json->loadFromJSON(collider->serialize());
  EXPECT_FALSE(json->getRenderCollider());

  net::Message message(net::MessageType::undefined);
  collider->pack(message);
  net::MessageReader reader(message);
  static_cast<void>(reader.read<ComponentType>());

  const auto wire = std::make_shared<SphereCollider>();
  wire->setRenderCollider(true);
  wire->unpack(reader);
  EXPECT_FALSE(wire->getRenderCollider());
}

TEST(ColliderPersistence, ABoxWithNoGizmoFlagKeyDefaultsToOff)
{
  const auto collider = std::make_shared<BoxCollider>();

  auto saved = collider->serialize();
  saved.erase("renderCollider");

  const auto loaded = std::make_shared<BoxCollider>();
  loaded->setRenderCollider(true);
  loaded->loadFromJSON(saved);

  EXPECT_FALSE(loaded->getRenderCollider());
}

TEST(ColliderPersistence, ASphereWithNoGizmoFlagKeyDefaultsToOff)
{
  const auto collider = std::make_shared<SphereCollider>();

  auto saved = collider->serialize();
  saved.erase("renderCollider");

  const auto loaded = std::make_shared<SphereCollider>();
  loaded->setRenderCollider(true);
  loaded->loadFromJSON(saved);

  EXPECT_FALSE(loaded->getRenderCollider());
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

TEST(ColliderPersistence, ASphereRoundTripsItsLayerMaskAndTrigger)
{
  const auto collider = std::make_shared<SphereCollider>();
  collider->setLayer(7);
  collider->setMask(0xF0u);
  collider->setIsTrigger(true);

  for (const auto& loaded : { throughJson(collider), throughTheWire(collider) })
  {
    EXPECT_EQ(loaded->getLayer(), 7u);
    EXPECT_EQ(loaded->getMask(), 0xF0u);
    EXPECT_TRUE(loaded->isTrigger());
  }
}

TEST(ColliderPersistence, ABoxWithNoTriggerLayerOrMaskKeysLoadsTheDefaults)
{
  auto saved = std::make_shared<BoxCollider>()->serialize();
  saved.erase("isTrigger");
  saved.erase("layer");
  saved.erase("mask");

  const auto loaded = std::make_shared<BoxCollider>();
  loaded->setIsTrigger(true);
  loaded->setLayer(9);
  loaded->setMask(0x3u);
  loaded->loadFromJSON(saved);

  EXPECT_FALSE(loaded->isTrigger());
  EXPECT_EQ(loaded->getLayer(), 0u);
  EXPECT_EQ(loaded->getMask(), 0xFFFFFFFFu);
}

TEST(ColliderPersistence, ASphereWithNoTriggerLayerOrMaskKeysLoadsTheDefaults)
{
  auto saved = std::make_shared<SphereCollider>()->serialize();
  saved.erase("isTrigger");
  saved.erase("layer");
  saved.erase("mask");

  const auto loaded = std::make_shared<SphereCollider>();
  loaded->setIsTrigger(true);
  loaded->setLayer(9);
  loaded->setMask(0x3u);
  loaded->loadFromJSON(saved);

  EXPECT_FALSE(loaded->isTrigger());
  EXPECT_EQ(loaded->getLayer(), 0u);
  EXPECT_EQ(loaded->getMask(), 0xFFFFFFFFu);
}

TEST(ColliderPersistence, TheLayerSetterClampsAtTheLastLayer)
{
  const auto box = std::make_shared<BoxCollider>();
  const auto sphere = std::make_shared<SphereCollider>();

  for (const auto& collider : std::initializer_list<std::shared_ptr<Collider>>{ box, sphere })
  {
    collider->setLayer(31);
    EXPECT_EQ(collider->getLayer(), 31u);

    collider->setLayer(32);
    EXPECT_EQ(collider->getLayer(), 31u);

    collider->setLayer(0xFFFFFFFFu);
    EXPECT_EQ(collider->getLayer(), 31u);

    collider->setLayer(30);
    EXPECT_EQ(collider->getLayer(), 30u);
  }
}

TEST(ColliderPersistence, ALayerAtTheBoundaryLoadsUnchangedAndOnePastItClamps)
{
  auto boxSaved = std::make_shared<BoxCollider>()->serialize();
  auto sphereSaved = std::make_shared<SphereCollider>()->serialize();

  for (auto* saved : { &boxSaved, &sphereSaved })
  {
    (*saved)["layer"] = 31u;
  }

  const auto box = std::make_shared<BoxCollider>();
  const auto sphere = std::make_shared<SphereCollider>();
  box->loadFromJSON(boxSaved);
  sphere->loadFromJSON(sphereSaved);
  EXPECT_EQ(box->getLayer(), 31u);
  EXPECT_EQ(sphere->getLayer(), 31u);

  boxSaved["layer"] = 32u;
  sphereSaved["layer"] = 99u;
  box->setLayer(2);
  sphere->setLayer(2);
  box->loadFromJSON(boxSaved);
  sphere->loadFromJSON(sphereSaved);
  EXPECT_EQ(box->getLayer(), 31u);
  EXPECT_EQ(sphere->getLayer(), 31u);
}

TEST(ColliderPersistence, AnOutOfRangeBoxLayerIsClampedOffTheWire)
{
  net::Message message(net::MessageType::undefined);
  message.write(ComponentType::SubComponentType_boxCollider);
  message.write(false);
  message.write(glm::vec3(0));
  message.write(glm::vec3(1));
  message.write(glm::vec3(0));
  message.write(false);
  message.write(uint32_t{ 32 });
  message.write(uint32_t{ 0xFFFFFFFFu });

  net::MessageReader reader(message);
  static_cast<void>(reader.read<ComponentType>());

  const auto collider = std::make_shared<BoxCollider>();
  collider->unpack(reader);

  EXPECT_EQ(collider->getLayer(), 31u);
}

TEST(ColliderPersistence, AConvexMeshKeepsItsGizmoFlagThroughBothPaths)
{
  const auto collider = std::make_shared<ConvexMeshCollider>();
  collider->setRenderCollider(true);

  EXPECT_TRUE(throughJson(collider)->getRenderCollider());
  EXPECT_TRUE(throughTheWire(collider)->getRenderCollider());

  collider->setRenderCollider(false);

  EXPECT_FALSE(throughJson(collider)->getRenderCollider());
  EXPECT_FALSE(throughTheWire(collider)->getRenderCollider());
}

TEST(ColliderPersistence, AConvexMeshRoundTripsItsLayerAndMask)
{
  const auto collider = std::make_shared<ConvexMeshCollider>();
  collider->setLayer(7);
  collider->setMask(0xF0u);
  collider->setIsTrigger(true);

  for (const auto& loaded : { throughJson(collider), throughTheWire(collider) })
  {
    EXPECT_EQ(loaded->getLayer(), 7u);
    EXPECT_EQ(loaded->getMask(), 0xF0u);
    EXPECT_TRUE(loaded->isTrigger());
  }
}

TEST(ColliderPersistence, AnOutOfRangeConvexMeshLayerIsClampedOnLoad)
{
  auto saved = std::make_shared<ConvexMeshCollider>()->serialize();
  saved["layer"] = 99u;

  const auto loaded = std::make_shared<ConvexMeshCollider>();
  loaded->loadFromJSON(saved);

  EXPECT_EQ(loaded->getLayer(), 31u);
}
