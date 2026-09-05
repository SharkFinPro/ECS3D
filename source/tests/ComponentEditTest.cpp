#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "Replication.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"

#include <Protocol.h>
#include <glm/vec3.hpp>
#include <memory>

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

  // Always a bug - corruption, a truncated read, or a protocol mismatch - and it must not be mistaken
  // for the routine case below.
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

  const auto donor = std::make_shared<Object>("Donor");
  scene.objectManager->addObject(donor);

  const auto collider = std::make_shared<BoxCollider>();
  donor->addComponent(collider);

  // The uuid resolves, but the object it names carries only a Transform.
  const auto edit = replication::buildComponentEdit(scene.object->getUUID(), collider);

  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, edit),
            replication::ComponentEditResult::unknownComponent);
}
