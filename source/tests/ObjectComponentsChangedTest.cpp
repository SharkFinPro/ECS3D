#include <gtest/gtest.h>

#include "TestScene.h"
#include "ComponentRegistry.h"
#include "Replication.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Script.h"

#include <Protocol.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
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

  uuids::uuid childUUID()
  {
    return uuids::uuid::from_string("323e4567-e89b-12d3-a456-426614174002").value();
  }

  // The target object plus one child, both at fixed uuids so a server- and a client-side build of this
  // tree name the same objects. The child carries its own RigidBody, so a test can tell "reconciled" (the
  // component survives) apart from "rebuilt from nothing" (it would not).
  std::shared_ptr<Object> addTargetWithChild(const Scene& scene)
  {
    const auto parent = addTargetObject(scene);

    auto child = std::make_shared<Object>("Child", childUUID());
    child->setParent(parent);
    scene.objectManager->addObject(child);
    child->addComponent(std::make_shared<RigidBody>());

    return parent;
  }

  std::shared_ptr<Script> addScript(const std::shared_ptr<Object>& object, const std::string& className,
                                    const nlohmann::json& fields)
  {
    auto script = std::make_shared<Script>(className);
    script->setFields(fields);
    object->addComponent(script);
    return script;
  }

  std::shared_ptr<Script> scriptNamed(const std::shared_ptr<Object>& object, const std::string& className)
  {
    for (const auto& component : object->getScripts())
    {
      if (const auto script = std::dynamic_pointer_cast<Script>(component);
          script && script->getClassName() == className)
      {
        return script;
      }
    }

    return nullptr;
  }

  // An object payload with no components, one script section carrying `tag`, and no children - what the
  // script section of Object::pack looks like when the tag is right.
  net::Message oneScriptSection(const ComponentType tag, const std::string& className)
  {
    net::Message message(net::MessageType::objectComponentsChanged);
    message.writeString(uuids::to_string(targetUUID()));
    message.writeString("Target");
    message.write<uint32_t>(0);
    message.write<uint32_t>(1);
    message.write(tag);
    message.writeString(className);
    message.writeString("[]");
    message.write<uint32_t>(0);

    return message;
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

TEST(ObjectComponentsChanged, RoundTripResyncsTheWholeSubtreeAndReconcilesTheChild)
{
  const auto server = makeScene();
  const auto serverParent = addTargetWithChild(server);
  serverParent->addComponent(std::make_shared<RigidBody>());

  // Object::pack recurses through children, so this one message carries the parent's added component and
  // the untouched child together.
  const auto message = replication::buildObjectComponentsChanged(*serverParent);

  const auto client = makeScene();
  const auto clientParent = addTargetWithChild(client);
  ASSERT_FALSE(hasRigidBody(clientParent));
  ASSERT_EQ(clientParent->getChildren().size(), 1u);
  const auto clientChildBefore = clientParent->getChildren()[0];

  replication::applyObjectComponentsChanged(*client.objectManager, message);

  EXPECT_TRUE(hasRigidBody(clientParent));

  // Reconciled in place, not duplicated or dropped: still exactly one child, the same Object instance
  // (Object::unpack matches an existing child by uuid rather than rebuilding it), uuid and component intact.
  ASSERT_EQ(clientParent->getChildren().size(), 1u);
  EXPECT_EQ(clientParent->getChildren()[0], clientChildBefore);
  EXPECT_EQ(clientParent->getChildren()[0]->getUUID(), childUUID());
  EXPECT_TRUE(hasRigidBody(clientParent->getChildren()[0]));
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

TEST(ObjectComponentsChanged, ASameClassScriptIsReconciledInPlaceAndADifferentClassIsAdded)
{
  const auto server = makeScene();
  const auto serverObject = addTargetObject(server);
  addScript(serverObject, "Spinner", { { "speed", 5 } });
  addScript(serverObject, "Bouncer", nlohmann::json::object());
  const auto message = replication::buildObjectComponentsChanged(*serverObject);

  const auto client = makeScene();
  const auto clientObject = addTargetObject(client);
  const auto existing = addScript(clientObject, "Spinner", { { "speed", 1 } });

  replication::applyObjectComponentsChanged(*client.objectManager, message);

  // The Spinner the client already had is the one that was updated, not a replacement; Bouncer is new.
  ASSERT_EQ(clientObject->getScripts().size(), 2u);
  EXPECT_EQ(scriptNamed(clientObject, "Spinner"), existing);
  EXPECT_EQ(existing->getFields().at("speed"), 5);
  EXPECT_NE(scriptNamed(clientObject, "Bouncer"), nullptr);
}

TEST(ObjectComponentsChanged, AScriptSectionWhoseTagIsNotAScriptIsRefused)
{
  const auto client = makeScene();
  const auto clientObject = addTargetObject(client);
  addScript(clientObject, "Spinner", nlohmann::json::object());

  EXPECT_THROW(replication::applyObjectComponentsChanged(
                 *client.objectManager, oneScriptSection(ComponentType::transform, "Intruder")),
               std::runtime_error);
  EXPECT_EQ(scriptNamed(clientObject, "Intruder"), nullptr);
  EXPECT_NE(scriptNamed(clientObject, "Spinner"), nullptr);
  EXPECT_EQ(client.objectManager->getObjectByUUID(targetUUID()), clientObject);

  // Positive control: the identical payload with the right tag is applied.
  EXPECT_NO_THROW(replication::applyObjectComponentsChanged(
    *client.objectManager, oneScriptSection(ComponentType::script, "Intruder")));
  EXPECT_NE(scriptNamed(clientObject, "Intruder"), nullptr);
}

TEST(ObjectComponentsChanged, AScriptTypeThisBuildDoesNotRegisterIsRefused)
{
  Scene bare(fixtures::Components::none);
  const auto bareObject = addTargetObject(bare);

  ASSERT_EQ(bare.componentRegistry->create("Script"), nullptr);
  EXPECT_THROW(replication::applyObjectComponentsChanged(
                 *bare.objectManager, oneScriptSection(ComponentType::script, "Spinner")),
               std::runtime_error);
  EXPECT_TRUE(bareObject->getScripts().empty());

  // Positive control: with the component registered the same payload attaches the script.
  const auto client = makeScene();
  const auto clientObject = addTargetObject(client);
  replication::applyObjectComponentsChanged(*client.objectManager,
                                            oneScriptSection(ComponentType::script, "Spinner"));
  EXPECT_NE(scriptNamed(clientObject, "Spinner"), nullptr);
}

TEST(ObjectComponentsChanged, LoadingAScriptWhenTheScriptTypeIsNotRegisteredThrows)
{
  const nlohmann::json body = {
    { "name", "Scripted" },
    { "uuid", "123e4567-e89b-12d3-a456-426614174000" },
    { "components", nlohmann::json::array() },
    { "scripts", nlohmann::json::array({ { { "type", "Script" }, { "className", "Spinner" } } }) },
    { "children", nlohmann::json::array() }
  };

  Scene bare(fixtures::Components::none);
  EXPECT_THROW(Object(body, bare.objectManager.get()), std::runtime_error);

  const auto registered = makeScene();
  const Object loaded(body, registered.objectManager.get());
  ASSERT_EQ(loaded.getScripts().size(), 1u);
  EXPECT_EQ(std::dynamic_pointer_cast<Script>(loaded.getScripts().front())->getClassName(), "Spinner");
}

TEST(ObjectComponentsChanged, ALoadFailureNamesTheObjectAndTheComponentThatBrokeIt)
{
  const auto scene = makeScene();
  const std::string uuid = "123e4567-e89b-12d3-a456-426614174000";

  const nlohmann::json brokenComponent = {
    { "name", "Lamp" },
    { "uuid", uuid },
    { "components", nlohmann::json::array({ { { "type", "Transform" } } }) },
    { "scripts", nlohmann::json::array() },
    { "children", nlohmann::json::array() }
  };

  try
  {
    const Object loaded(brokenComponent, scene.objectManager.get());
    FAIL() << "a Transform with no fields should not load";
  }
  catch (const std::runtime_error& error)
  {
    const std::string message = error.what();
    EXPECT_NE(message.find("Lamp"), std::string::npos) << message;
    EXPECT_NE(message.find(uuid), std::string::npos) << message;
    EXPECT_NE(message.find("Transform"), std::string::npos) << message;
  }

  const nlohmann::json brokenScript = {
    { "name", "Lamp" },
    { "uuid", uuid },
    { "components", nlohmann::json::array() },
    { "scripts", nlohmann::json::array({ { { "type", "Script" }, { "className", 5 } } }) },
    { "children", nlohmann::json::array() }
  };

  try
  {
    const Object loaded(brokenScript, scene.objectManager.get());
    FAIL() << "a Script whose class name is not a string should not load";
  }
  catch (const std::runtime_error& error)
  {
    const std::string message = error.what();
    EXPECT_NE(message.find("Lamp"), std::string::npos) << message;
    EXPECT_NE(message.find("Script"), std::string::npos) << message;
  }
}
