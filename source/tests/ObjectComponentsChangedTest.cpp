#include <gtest/gtest.h>

#include "TestScene.h"
#include "ComponentRegistry.h"
#include "Replication.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/RigidBody.h"

#include <Protocol.h>
#include <cstddef>
#include <memory>
#include <uuid.h>

namespace {
  using fixtures::makeScene;
  using fixtures::Scene;

  // Both sides of a round-trip test agree the target object is this uuid, the way a client that already
  // has the object (from an earlier snapshot/spawn) would.
  uuids::uuid targetUUID()
  {
    return uuids::uuid::from_string("223e4567-e89b-12d3-a456-426614174001").value();
  }

  std::shared_ptr<Object> addTargetObject(const Scene& scene)
  {
    auto object = std::make_shared<Object>("Target", targetUUID());
    scene.objectManager->addObject(object);
    return object;
  }

  bool hasRigidBody(const std::shared_ptr<Object>& object)
  {
    return object->getComponent<RigidBody>(ComponentType::rigidBody) != nullptr;
  }
}

TEST(ObjectComponentsChanged, RoundTripAppliesAnAddedComponent)
{
  const auto server = makeScene();
  const auto serverObject = addTargetObject(server);
  serverObject->addComponent(std::make_shared<RigidBody>());

  const auto message = replication::buildObjectComponentsChanged(*serverObject);

  // A separate ObjectManager holding the object's old state, the way a client's replicated scene does -
  // same uuid, but built before the server-side add happened.
  const auto client = makeScene();
  const auto clientObject = addTargetObject(client);
  ASSERT_FALSE(hasRigidBody(clientObject));

  replication::applyObjectComponentsChanged(*client.objectManager, message);

  EXPECT_TRUE(hasRigidBody(clientObject));
}

TEST(ObjectComponentsChanged, RoundTripAppliesARemovedComponent)
{
  const auto server = makeScene();
  const auto serverObject = addTargetObject(server);
  // The server object never carries a RigidBody - as if a script had already removed it.

  const auto client = makeScene();
  const auto clientObject = addTargetObject(client);
  clientObject->addComponent(std::make_shared<RigidBody>());
  ASSERT_TRUE(hasRigidBody(clientObject));

  const auto message = replication::buildObjectComponentsChanged(*serverObject);
  replication::applyObjectComponentsChanged(*client.objectManager, message);

  EXPECT_FALSE(hasRigidBody(clientObject));
}

TEST(ObjectComponentsChanged, AnUnknownUUIDIsASafeNoOp)
{
  const auto server = makeScene();
  const auto serverObject = addTargetObject(server);
  serverObject->addComponent(std::make_shared<RigidBody>());
  const auto message = replication::buildObjectComponentsChanged(*serverObject);

  // Nothing in this manager carries targetUUID (or any object at all).
  const auto client = makeScene();

  EXPECT_NO_THROW(replication::applyObjectComponentsChanged(*client.objectManager, message));
  EXPECT_TRUE(client.objectManager->getAllObjects().empty());
}

TEST(ObjectComponentsChanged, ATruncatedMessageIsRefused)
{
  const auto server = makeScene();
  const auto serverObject = addTargetObject(server);
  serverObject->addComponent(std::make_shared<RigidBody>());
  const auto full = replication::buildObjectComponentsChanged(*serverObject);

  // Every byte but the component's own body - enough to claim a component the reader then runs out
  // reading, the same truncation shape ObjectSpawnTest's firstBytes helper exercises for objectSpawned.
  net::Message truncated(net::MessageType::objectComponentsChanged);
  const auto bytes = full.bytes();
  ASSERT_GT(bytes.size(), 4u);
  for (std::size_t i = 0; i < bytes.size() - 4; ++i)
  {
    truncated.write(bytes[i]);
  }

  const auto client = makeScene();
  addTargetObject(client);

  EXPECT_ANY_THROW(replication::applyObjectComponentsChanged(*client.objectManager, truncated));
}
