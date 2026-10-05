#include <gtest/gtest.h>

#include "TestScene.h"
#include "Replication.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Transform.h"

#include <Protocol.h>
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <uuid.h>
#include <vector>

namespace {
  using fixtures::expectNear;
  using fixtures::makeScene;
  using fixtures::Scene;
  using fixtures::transformOf;

  // Every object as "uuid -> parent uuid" (empty for a root), sorted so two scenes holding the same
  // tree compare equal whatever order each registered its objects in.
  std::vector<std::pair<std::string, std::string>> parentage(const ObjectManager& manager)
  {
    std::vector<std::pair<std::string, std::string>> result;
    for (const auto& object : manager.getAllObjects())
    {
      const auto parent = object->getParent();
      result.emplace_back(uuids::to_string(object->getUUID()),
                          parent ? uuids::to_string(parent->getUUID()) : std::string{});
    }

    std::ranges::sort(result);
    return result;
  }

  // A second scene holding the same objects under the same uuids, the way a client's replicated view
  // matches the server's scene.
  Scene replicaOf(const Scene& source)
  {
    Scene replica;
    replica.objectManager->restoreFromJSON(source.objectManager->serialize().at("objects"));
    return replica;
  }
}

TEST(ObjectDestroyed, RemovesTheNamedObjectFromAReplicatedScene)
{
  const auto server = makeScene();
  const auto doomed = addObject(server, "Doomed");
  const auto keeper = addObject(server, "Keeper");

  const auto client = replicaOf(server);
  ASSERT_NE(client.objectManager->getObjectByUUID(doomed->getUUID()), nullptr);

  replication::applyObjectDestroyed(*client.objectManager, replication::buildObjectDestroyed(doomed->getUUID()));

  EXPECT_EQ(client.objectManager->getObjectByUUID(doomed->getUUID()), nullptr);
  EXPECT_NE(client.objectManager->getObjectByUUID(keeper->getUUID()), nullptr);
  EXPECT_EQ(client.objectManager->getAllObjects().size(), 1u);
  EXPECT_EQ(client.objectManager->getObjects().size(), 1u);
}

TEST(ObjectDestroyed, AnUnknownOrMalformedUuidChangesNothing)
{
  const auto scene = makeScene();
  const auto doomed = addObject(scene, "Doomed");
  const auto before = parentage(*scene.objectManager);

  const auto unknown = uuids::uuid::from_string("123e4567-e89b-12d3-a456-426614174000").value();
  replication::applyObjectDestroyed(*scene.objectManager, replication::buildObjectDestroyed(unknown));
  EXPECT_EQ(parentage(*scene.objectManager), before);

  net::Message malformed(net::MessageType::objectDestroyed);
  malformed.writeString("not-a-uuid");
  replication::applyObjectDestroyed(*scene.objectManager, malformed);
  EXPECT_EQ(parentage(*scene.objectManager), before);

  // Positive control: the same scene does drop an object that is named properly.
  replication::applyObjectDestroyed(*scene.objectManager, replication::buildObjectDestroyed(doomed->getUUID()));
  EXPECT_EQ(scene.objectManager->getObjectByUUID(doomed->getUUID()), nullptr);
}

TEST(ObjectDestroyed, ADestroyedParentLeavesItsChildrenWhereTheServerLeavesThem)
{
  const auto server = makeScene();
  const auto first = addObject(server, "First");
  const auto grandparent = addObject(server, "Grandparent");
  const auto last = addObject(server, "Last");
  const auto parent = addChildObject(server, "Parent", grandparent);
  const auto sibling = addChildObject(server, "Sibling", grandparent);
  const auto childA = addChildObject(server, "ChildA", parent);
  const auto childB = addChildObject(server, "ChildB", parent);

  transformOf(grandparent)->setPosition({ 0, 5, 0 });
  transformOf(parent)->setPosition({ 10, 0, 0 });
  transformOf(childA)->setPosition({ 1, 2, 3 });
  transformOf(childB)->setPosition({ 4, 5, 6 });

  const auto client = replicaOf(server);
  const auto childAWorld = transformOf(childA)->getPosition();
  const auto childBWorld = transformOf(childB)->getPosition();

  // What a script destroy does on the server (WorldBindings marks it, ServerApp drains it after the tick),
  // then what the client does with the message that announces it.
  const auto parentUUID = parent->getUUID();
  const auto message = replication::buildObjectDestroyed(parentUUID);
  ASSERT_TRUE(server.objectManager->removeObject(parent));
  server.objectManager->deleteObjectsMarkedForDeletion();
  replication::applyObjectDestroyed(*client.objectManager, message);

  EXPECT_EQ(parentage(*client.objectManager), parentage(*server.objectManager));
  EXPECT_EQ(client.objectManager->getObjectByUUID(parentUUID), nullptr);

  // Promoted into the destroyed parent's slot under the grandparent, in order, not appended at the end.
  const auto clientGrandparent = client.objectManager->getObjectByUUID(grandparent->getUUID());
  ASSERT_NE(clientGrandparent, nullptr);
  ASSERT_EQ(clientGrandparent->getChildren().size(), 3u);
  EXPECT_EQ(clientGrandparent->getChildren()[0]->getUUID(), childA->getUUID());
  EXPECT_EQ(clientGrandparent->getChildren()[1]->getUUID(), childB->getUUID());
  EXPECT_EQ(clientGrandparent->getChildren()[2]->getUUID(), sibling->getUUID());

  // Same world placement on both sides, and unchanged by the destroy.
  for (const auto* sideManager : { server.objectManager.get(), client.objectManager.get() })
  {
    const auto a = sideManager->getObjectByUUID(childA->getUUID());
    const auto b = sideManager->getObjectByUUID(childB->getUUID());
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    expectNear("child A world position", transformOf(a)->getPosition(), childAWorld);
    expectNear("child B world position", transformOf(b)->getPosition(), childBWorld);
  }

  // Roots untouched.
  EXPECT_EQ(client.objectManager->getObjects().size(), server.objectManager->getObjects().size());
  EXPECT_NE(client.objectManager->getObjectByUUID(first->getUUID()), nullptr);
  EXPECT_NE(client.objectManager->getObjectByUUID(last->getUUID()), nullptr);
}
