#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "Replication.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Transform.h"
#include "WireTypes.h"

#include <Protocol.h>
#include <glm/vec3.hpp>
#include <cmath>
#include <stdexcept>
#include <limits>
#include <memory>
#include <string>
#include <uuid.h>

namespace {
  struct Scene {
    std::shared_ptr<ComponentRegistry> componentRegistry = std::make_shared<ComponentRegistry>();
    std::unique_ptr<ObjectManager> objectManager;
  };

  Scene makeScene()
  {
    Scene scene;
    registerDataComponents(*scene.componentRegistry);
    scene.objectManager = std::make_unique<ObjectManager>(scene.componentRegistry);

    return scene;
  }

  std::shared_ptr<Object> addObject(const Scene& scene, const std::string& name,
                                    const std::shared_ptr<Object>& parent = nullptr)
  {
    auto object = std::make_shared<Object>(name);
    object->setParent(parent);
    scene.objectManager->addObject(object);

    return object;
  }

  std::shared_ptr<Transform> transformOf(const std::shared_ptr<Object>& object)
  {
    return object->getComponent<Transform>(ComponentType::transform);
  }

  // The way a view actually comes to share uuids with the authority: it is sent the scene. Cloning any
  // other way would mean inventing matching uuids, which is not something the engine lets a caller do -
  // and pinning the delta against a scene built differently from the real one would prove less.
  void snapshotInto(const Scene& source, const Scene& target)
  {
    net::Message snapshot(net::MessageType::snapshot);
    source.objectManager->pack(snapshot);

    net::MessageReader reader(snapshot);
    target.objectManager->unpack(reader);
  }

  net::Message deltaOf(const Scene& scene)
  {
    net::Message message(net::MessageType::stateDelta);
    replication::packStateDelta(message, *scene.objectManager);

    return message;
  }

  std::shared_ptr<Object> findByName(const Scene& scene, const std::string& name)
  {
    for (const auto& object : scene.objectManager->getAllObjects())
    {
      if (object->getName() == name)
      {
        return object;
      }
    }

    // Thrown rather than returned null: every caller here immediately dereferences it, and a message
    // naming the object reads better than a crash in the line after.
    throw std::runtime_error("no object named " + name);
  }

  void expectNear(const char* what, const glm::vec3& actual, const glm::vec3& expected)
  {
    constexpr float tolerance = 1e-5f;
    SCOPED_TRACE(::testing::Message()
                 << what
                 << ": expected (" << expected.x << ", " << expected.y << ", " << expected.z << ")"
                 << ", actual (" << actual.x << ", " << actual.y << ", " << actual.z << ")");

    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
  }
}

TEST(StateDelta, ReproducesATransformOnTheReceivingScene)
{
  const auto source = makeScene();
  const auto target = makeScene();

  const auto object = addObject(source, "Object");
  snapshotInto(source, target);

  transformOf(object)->setPosition({ 1.5f, -2.25f, 3.0f });
  transformOf(object)->setRotation({ 10, 20, 30 });
  transformOf(object)->setScale({ 2, 4, 8 });

  replication::unpackStateDelta(*target.objectManager, deltaOf(source));

  const auto replicated = transformOf(findByName(target, "Object"));
  ASSERT_NE(replicated, nullptr);

  // Every value the delta carries, on the way in and on the way out. This is the stream that keeps two
  // players seeing the same world, and drift here does not crash - it just makes them disagree.
  expectNear("position", replicated->getLocalPosition(), { 1.5f, -2.25f, 3.0f });
  expectNear("rotation", replicated->getLocalRotation(), { 10, 20, 30 });
  expectNear("scale", replicated->getLocalScale(), { 2, 4, 8 });
}

TEST(StateDelta, CarriesLocalTransformsSoAHierarchyIsNotDoubleCounted)
{
  const auto source = makeScene();
  const auto target = makeScene();

  const auto parent = addObject(source, "Parent");
  const auto child = addObject(source, "Child", parent);
  snapshotInto(source, target);

  transformOf(parent)->setPosition({ 10, 0, 0 });
  transformOf(child)->setPosition({ 1, 0, 0 });

  replication::unpackStateDelta(*target.objectManager, deltaOf(source));

  const auto replicatedChild = transformOf(findByName(target, "Child"));
  ASSERT_NE(replicatedChild, nullptr);

  // The wire carries the child's own offset, and the receiver walks the parents itself. Sending the
  // combined value instead would land the child at 21 rather than 11 - and only under hierarchy, so a
  // flat scene would look perfectly fine.
  expectNear("child local", replicatedChild->getLocalPosition(), { 1, 0, 0 });
  expectNear("child world", replicatedChild->getPosition(), { 11, 0, 0 });
}

TEST(StateDelta, AnEmptySceneStillPacksACount)
{
  const auto scene = makeScene();

  const auto message = deltaOf(scene);

  // The count leads the entries, so an empty scene is four bytes rather than nothing - and the receiver
  // reads a count of zero instead of running off the end of the payload.
  ASSERT_EQ(message.size(), sizeof(uint32_t));

  net::MessageReader reader(message);
  EXPECT_EQ(reader.read<uint32_t>(), 0u);
}

TEST(StateDelta, SkipsAnObjectWhoseTransformIsNotFinite)
{
  const auto source = makeScene();
  const auto target = makeScene();

  const auto healthy = addObject(source, "Healthy");
  const auto broken = addObject(source, "Broken");
  snapshotInto(source, target);

  transformOf(healthy)->setPosition({ 1, 2, 3 });
  transformOf(broken)->setPosition({ std::numeric_limits<float>::quiet_NaN(), 0, 0 });

  const auto message = deltaOf(source);

  // One entry, not two: a NaN written into the receiver's scene would spread through every world
  // transform below it and never come back, so it is dropped at the sender.
  net::MessageReader reader(message);
  EXPECT_EQ(reader.read<uint32_t>(), 1u);

  replication::unpackStateDelta(*target.objectManager, message);

  expectNear("healthy", transformOf(findByName(target, "Healthy"))->getLocalPosition(), { 1, 2, 3 });
  EXPECT_TRUE(std::isfinite(transformOf(findByName(target, "Broken"))->getLocalPosition().x));
}

TEST(StateDelta, AnEntryForAnObjectTheReceiverDoesNotHaveDoesNotDerailTheRest)
{
  const auto source = makeScene();
  const auto target = makeScene();

  const auto first = addObject(source, "First");
  const auto unknown = addObject(source, "Unknown");
  const auto third = addObject(source, "Third");
  snapshotInto(source, target);

  // The receiver drops the middle one. Routine rather than exotic: the authority streams a delta for
  // everything it holds, and a view is always a little behind - it can have removed an object the
  // authority has not stopped sending yet. The entry order follows registration, so the one it cannot
  // resolve sits between two it can.
  target.objectManager->removeObject(findByName(target, "Unknown"));
  target.objectManager->deleteObjectsMarkedForDeletion();

  transformOf(first)->setPosition({ 1, 0, 0 });
  transformOf(unknown)->setPosition({ 2, 0, 0 });
  transformOf(third)->setPosition({ 3, 0, 0 });

  replication::unpackStateDelta(*target.objectManager, deltaOf(source));

  // The unknown entry is skipped, but its bytes are still consumed - so everything after it lands where
  // it should instead of being read at an offset.
  expectNear("first", transformOf(findByName(target, "First"))->getLocalPosition(), { 1, 0, 0 });
  expectNear("third", transformOf(findByName(target, "Third"))->getLocalPosition(), { 3, 0, 0 });
}

TEST(StateDelta, AnEntryWithAnUnreadableUuidDoesNotDerailTheRest)
{
  const auto source = makeScene();
  const auto target = makeScene();

  const auto object = addObject(source, "Object");
  snapshotInto(source, target);

  transformOf(object)->setPosition({ 7, 8, 9 });

  // Two entries by hand: a uuid that does not parse, then the real one. Same reasoning as above, for the
  // other way an entry can be unusable.
  net::Message message(net::MessageType::stateDelta);
  message.write<uint32_t>(2);

  message.writeString("not-a-uuid");
  message.write(glm::vec3(0));
  message.write(glm::vec3(0));
  message.write(glm::vec3(0));

  message.writeString(uuids::to_string(object->getUUID()));
  message.write(glm::vec3(7, 8, 9));
  message.write(glm::vec3(0));
  message.write(glm::vec3(1));

  replication::unpackStateDelta(*target.objectManager, message);

  expectNear("position", transformOf(findByName(target, "Object"))->getLocalPosition(), { 7, 8, 9 });
}

TEST(StateDelta, ATruncatedDeltaThrowsRatherThanWritingHalfOfIt)
{
  const auto target = makeScene();
  addObject(target, "Object");

  // Claims an entry and stops after the uuid. The reader throws on the underflow, which the client's run
  // loop catches - the alternative is reading whatever follows in memory as a transform.
  net::Message message(net::MessageType::stateDelta);
  message.write<uint32_t>(1);
  message.writeString("123e4567-e89b-12d3-a456-426614174000");

  EXPECT_THROW(replication::unpackStateDelta(*target.objectManager, message), std::runtime_error);
}
