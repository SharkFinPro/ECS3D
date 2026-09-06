#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "Replication.h"
#include "TestPrinters.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Transform.h"
#include "WireTypes.h"

#include <Protocol.h>
#include <glm/vec3.hpp>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
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

  // Thrown rather than returned null, like findByName below: every caller dereferences the result on the
  // same line, so a message beats a crash in the line after.
  std::shared_ptr<Transform> transformOf(const std::shared_ptr<Object>& object)
  {
    auto transform = object->getComponent<Transform>(ComponentType::transform);
    if (!transform)
    {
      throw std::runtime_error(object->getName() + " has no transform");
    }

    return transform;
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

    throw std::runtime_error("no object named " + name);
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

  // The uuid strings a delta carries, in the order it carries them. Reading them back is what lets a
  // test say which entry sat where rather than assuming the sender's iteration order stays put.
  std::vector<std::string> entryUuids(const net::Message& message)
  {
    net::MessageReader reader(message);

    std::vector<std::string> uuidStrings;
    const uint32_t count = reader.read<uint32_t>();
    for (uint32_t i = 0; i < count; ++i)
    {
      uuidStrings.push_back(reader.readString());
      static_cast<void>(reader.read<glm::vec3>());
      static_cast<void>(reader.read<glm::vec3>());
      static_cast<void>(reader.read<glm::vec3>());
    }

    return uuidStrings;
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

  // Compared exactly, because the path is exact: every value goes bit_cast, memcpy, bit_cast with no
  // arithmetic anywhere. A tolerance here would tell a reader the wire is approximate when it is not,
  // and would hide a single-bit corruption.
  EXPECT_EQ(replicated->getLocalPosition(), glm::vec3(1.5f, -2.25f, 3.0f));
  EXPECT_EQ(replicated->getLocalRotation(), glm::vec3(10, 20, 30));
  EXPECT_EQ(replicated->getLocalScale(), glm::vec3(2, 4, 8));
}

TEST(StateDelta, SendsWhatARunningSceneShowsRatherThanWhatItWouldSave)
{
  const auto source = makeScene();
  const auto target = makeScene();

  const auto object = addObject(source, "Object");
  snapshotInto(source, target);

  // The authority is always a started scene and a view never is, so a value leaves the live slot and
  // arrives in the authored one. Every other test here runs both ends stopped, where the two agree -
  // which is exactly where a regression in that routing would hide.
  source.objectManager->start();
  transformOf(object)->setPosition({ 4, 5, 6 });

  replication::unpackStateDelta(*target.objectManager, deltaOf(source));

  EXPECT_EQ(transformOf(findByName(target, "Object"))->getLocalPosition(), glm::vec3(4, 5, 6));

  // And the run did not touch what the scene would save.
  source.objectManager->stop();
  EXPECT_EQ(transformOf(object)->getLocalPosition(), glm::vec3(0));
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

  const auto message = deltaOf(source);
  EXPECT_EQ(entryUuids(message).size(), 2u);

  replication::unpackStateDelta(*target.objectManager, message);

  const auto replicatedChild = transformOf(findByName(target, "Child"));

  // The wire carries the child's own offset, and the receiver walks the parents itself. Sending the
  // combined value instead would land the child at 21 rather than 11 - and only under hierarchy, so a
  // flat scene would look perfectly fine.
  EXPECT_EQ(replicatedChild->getLocalPosition(), glm::vec3(1, 0, 0));
  EXPECT_EQ(replicatedChild->getPosition(), glm::vec3(11, 0, 0));
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

  const auto healthy = addObject(source, "Healthy");
  const auto notANumber = addObject(source, "NotANumber");
  const auto infinite = addObject(source, "Infinite");
  const auto spinning = addObject(source, "Spinning");
  const auto scaled = addObject(source, "Scaled");

  transformOf(healthy)->setPosition({ 1, 2, 3 });
  transformOf(notANumber)->setPosition({ std::numeric_limits<float>::quiet_NaN(), 0, 0 });
  transformOf(infinite)->setPosition({ std::numeric_limits<float>::infinity(), 0, 0 });

  // All three vectors are checked, not just the position: a non-finite rotation or scale reaches the
  // receiver's world transforms just as surely.
  transformOf(spinning)->setRotation({ 0, std::numeric_limits<float>::quiet_NaN(), 0 });
  transformOf(scaled)->setScale({ 0, 0, std::numeric_limits<float>::infinity() });

  const auto sent = entryUuids(deltaOf(source));

  // A NaN written into the receiver's scene spreads through every world transform below it and never
  // comes back, so it is dropped at the sender rather than filtered at the far end.
  ASSERT_EQ(sent.size(), 1u);
  EXPECT_EQ(sent.front(), uuids::to_string(healthy->getUUID()));
}

TEST(StateDelta, SkipsAnObjectWithNoTransformAtAll)
{
  const auto source = makeScene();

  const auto healthy = addObject(source, "Healthy");

  auto bare = std::make_shared<Object>(std::vector<std::shared_ptr<Component>>{}, "Bare");
  source.objectManager->addObject(bare);

  const auto sent = entryUuids(deltaOf(source));

  // Nothing in the engine builds one of these today, but the delta is the wrong place to find out: the
  // sender reads three vectors off a component that is not there.
  ASSERT_EQ(sent.size(), 1u);
  EXPECT_EQ(sent.front(), uuids::to_string(healthy->getUUID()));
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
  // authority has not stopped sending yet.
  target.objectManager->removeObject(findByName(target, "Unknown"));
  target.objectManager->deleteObjectsMarkedForDeletion();

  transformOf(first)->setPosition({ 1, 0, 0 });
  transformOf(unknown)->setPosition({ 2, 0, 0 });
  transformOf(third)->setPosition({ 3, 0, 0 });

  const auto message = deltaOf(source);

  // Asserted rather than assumed: the whole point is an unresolvable entry with entries after it, and
  // if the sender's order ever changed this would quietly stop testing that.
  const auto sent = entryUuids(message);
  ASSERT_EQ(sent.size(), 3u);
  ASSERT_EQ(sent[1], uuids::to_string(unknown->getUUID()));

  replication::unpackStateDelta(*target.objectManager, message);

  // The unknown entry is skipped, but its bytes are still consumed - so everything after it lands where
  // it should instead of being read at an offset.
  EXPECT_EQ(transformOf(findByName(target, "First"))->getLocalPosition(), glm::vec3(1, 0, 0));
  EXPECT_EQ(transformOf(findByName(target, "Third"))->getLocalPosition(), glm::vec3(3, 0, 0));
}

TEST(StateDelta, AnEntryWithAnUnreadableUuidDoesNotDerailTheRest)
{
  const auto source = makeScene();
  const auto target = makeScene();

  const auto object = addObject(source, "Object");
  snapshotInto(source, target);

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
  message.write(glm::vec3(90, 0, 0));
  message.write(glm::vec3(2, 2, 2));

  replication::unpackStateDelta(*target.objectManager, message);

  const auto replicated = transformOf(findByName(target, "Object"));

  // All three vectors, in the order the sender writes them. A round trip cannot catch a symmetric swap
  // of rotation and scale, and this is the only place the field order is pinned against a payload
  // written by hand - which matters, because Transform::pack orders the same three the other way.
  EXPECT_EQ(replicated->getLocalPosition(), glm::vec3(7, 8, 9));
  EXPECT_EQ(replicated->getLocalRotation(), glm::vec3(90, 0, 0));
  EXPECT_EQ(replicated->getLocalScale(), glm::vec3(2, 2, 2));
}

TEST(StateDelta, ATruncatedDeltaThrowsAfterApplyingWhatItAlreadyRead)
{
  const auto source = makeScene();
  const auto target = makeScene();

  const auto object = addObject(source, "Object");
  snapshotInto(source, target);

  net::Message message(net::MessageType::stateDelta);
  message.write<uint32_t>(2);

  message.writeString(uuids::to_string(object->getUUID()));
  message.write(glm::vec3(1, 2, 3));
  message.write(glm::vec3(0));
  message.write(glm::vec3(1));

  // The second entry stops after its uuid.
  message.writeString(uuids::to_string(object->getUUID()));

  EXPECT_THROW(replication::unpackStateDelta(*target.objectManager, message), std::runtime_error);

  // Entries are applied as they are read - nothing is staged and nothing is rolled back - so the first
  // one is already in the scene when the second runs out. Pinned because it is the opposite of what a
  // caller would assume from a function that threw, and the next tick's delta is what repairs it.
  EXPECT_EQ(transformOf(findByName(target, "Object"))->getLocalPosition(), glm::vec3(1, 2, 3));
}

TEST(StateDelta, AnEmptyPayloadThrowsRatherThanReadingACountThatIsNotThere)
{
  const auto target = makeScene();

  const net::Message message(net::MessageType::stateDelta);

  EXPECT_THROW(replication::unpackStateDelta(*target.objectManager, message), std::runtime_error);
}
