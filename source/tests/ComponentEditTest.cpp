#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "Replication.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Transform.h"
#include "objects/components/Script.h"
#include "objects/components/collisions/BoxCollider.h"

#include <Protocol.h>
#include <glm/vec3.hpp>
#include <cstddef>
#include <memory>
#include <nlohmann/json.hpp>
#include <uuid.h>

namespace {
  struct Scene {
    std::shared_ptr<ComponentRegistry> componentRegistry = std::make_shared<ComponentRegistry>();
    std::unique_ptr<ObjectManager> objectManager;
    std::shared_ptr<Object> object;
  };

  Scene makeScene()
  {
    Scene scene;
    registerDataComponents(*scene.componentRegistry);
    scene.objectManager = std::make_unique<ObjectManager>(scene.componentRegistry);

    scene.object = std::make_shared<Object>("Object");
    scene.objectManager->addObject(scene.object);

    return scene;
  }

  std::shared_ptr<Transform> transformOf(const std::shared_ptr<Object>& object)
  {
    return object->getComponent<Transform>(ComponentType::transform);
  }

  net::Message withoutTheLastBytes(const net::Message& source, const std::size_t dropped)
  {
    net::Message result(net::MessageType::editComponent);

    const auto bytes = source.bytes();
    for (std::size_t i = 0; i + dropped < bytes.size(); ++i)
    {
      result.write(bytes[i]);
    }

    return result;
  }
}

TEST(ComponentEdit, AppliesAnEditForAKnownObject)
{
  const auto scene = makeScene();
  const auto transform = transformOf(scene.object);

  transform->setPosition({ 1, 2, 3 });
  const auto edit = replication::buildComponentEdit(scene.object->getUUID(), transform);
  transform->setPosition({ 0, 0, 0 });

  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, edit),
            replication::ComponentEditResult::applied);
  EXPECT_EQ(transform->getLocalPosition(), glm::vec3(1, 2, 3));
}

TEST(ComponentEdit, ReportsAUuidThatDoesNotParseAsMalformed)
{
  const auto scene = makeScene();

  net::Message edit(net::MessageType::editComponent);
  edit.writeString("not-a-uuid");

  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, edit),
            replication::ComponentEditResult::malformedPayload);
}

TEST(ComponentEdit, ReportsATruncatedPayloadAsMalformed)
{
  const auto scene = makeScene();

  // A well-formed uuid and nothing after it, so the reader runs off the end of the payload.
  net::Message edit(net::MessageType::editComponent);
  edit.writeString(uuids::to_string(scene.object->getUUID()));

  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, edit),
            replication::ComponentEditResult::malformedPayload);
}

TEST(ComponentEdit, ReportsAPayloadThatRunsOutMidComponentAsPartiallyApplied)
{
  const auto scene = makeScene();

  const auto collider = std::make_shared<BoxCollider>();
  scene.object->addComponent(collider);

  collider->setScale({ 4, 5, 6 });
  const auto edit = replication::buildComponentEdit(scene.object->getUUID(), collider);
  collider->setScale({ 1, 1, 1 });

  // A component unpacks field by field as it reads, so losing the tail leaves it half written - which is
  // a different problem from a payload that never started applying.
  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, withoutTheLastBytes(edit, 4)),
            replication::ComponentEditResult::partiallyApplied);
  EXPECT_EQ(collider->getLocalScale(), glm::vec3(4, 5, 6));
}

TEST(ComponentEdit, ReportsAnObjectItDoesNotHaveAsUnknown)
{
  const auto scene = makeScene();

  // Built against another scene, which is what a view legitimately sees when the server rebroadcasts an
  // edit for an object it has not been sent yet or has already dropped.
  const auto elsewhere = makeScene();
  const auto edit = replication::buildComponentEdit(elsewhere.object->getUUID(), transformOf(elsewhere.object));

  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, edit),
            replication::ComponentEditResult::unknownObject);
}

TEST(ComponentEdit, ReportsAComponentTheObjectDoesNotHaveAsUnknown)
{
  const auto scene = makeScene();

  // The uuid resolves, but the object it names carries only a Transform.
  const auto edit = replication::buildComponentEdit(scene.object->getUUID(),
                                                    std::make_shared<BoxCollider>());

  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, edit),
            replication::ComponentEditResult::unknownComponent);
}

TEST(ComponentEdit, MapsAColliderSubtypeBackToTheComponentItIsStoredUnder)
{
  const auto scene = makeScene();

  const auto collider = std::make_shared<BoxCollider>();
  scene.object->addComponent(collider);

  collider->setScale({ 4, 5, 6 });
  const auto edit = replication::buildComponentEdit(scene.object->getUUID(), collider);
  collider->setScale({ 1, 1, 1 });

  // A collider packs its subtype as the discriminator but is stored under the parent collider type, so
  // this is the one component whose lookup needs a mapping step.
  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, edit),
            replication::ComponentEditResult::applied);
  EXPECT_EQ(collider->getLocalScale(), glm::vec3(4, 5, 6));
}

TEST(ComponentEdit, FindsTheRightScriptByClassName)
{
  const auto scene = makeScene();

  const auto first = std::make_shared<Script>();
  first->setClassName("First");
  scene.object->addComponent(first);

  const auto second = std::make_shared<Script>();
  second->setClassName("Second");
  scene.object->addComponent(second);

  second->setFields(nlohmann::json{ { "speed", 4 } });
  const auto edit = replication::buildComponentEdit(scene.object->getUUID(), second);
  second->setFields(nlohmann::json::object());

  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, edit),
            replication::ComponentEditResult::applied);
  EXPECT_EQ(second->getFields().at("speed"), 4);
  EXPECT_TRUE(first->getFields().empty());
}

TEST(ComponentEdit, ReportsAScriptClassTheObjectDoesNotCarryAsUnknown)
{
  const auto scene = makeScene();

  const auto present = std::make_shared<Script>();
  present->setClassName("Present");
  scene.object->addComponent(present);

  const auto absent = std::make_shared<Script>();
  absent->setClassName("Absent");

  const auto edit = replication::buildComponentEdit(scene.object->getUUID(), absent);

  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, edit),
            replication::ComponentEditResult::unknownComponent);
}
