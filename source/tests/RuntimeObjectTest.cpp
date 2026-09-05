#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "Replication.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/collisions/BoxCollider.h"

#include <Protocol.h>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <memory>
#include <utility>

namespace {
  std::unique_ptr<ObjectManager> makeManager(const std::shared_ptr<ComponentRegistry>& componentRegistry)
  {
    registerDataComponents(*componentRegistry);

    return std::make_unique<ObjectManager>(componentRegistry);
  }

  // An object carrying a collider whose scale is the value under test, built but not yet registered.
  std::pair<std::shared_ptr<Object>, std::shared_ptr<BoxCollider>> makeObjectWithCollider()
  {
    auto object = std::make_shared<Object>("Object");
    auto collider = std::make_shared<BoxCollider>();
    object->addComponent(collider);

    return { object, collider };
  }
}

TEST(RuntimeObject, AnObjectAddedToARunningSceneIsStarted)
{
  const auto componentRegistry = std::make_shared<ComponentRegistry>();
  const auto objectManager = makeManager(componentRegistry);

  objectManager->start();

  const auto [object, collider] = makeObjectWithCollider();
  objectManager->addObject(object);

  collider->setScale(glm::vec3(4));

  EXPECT_EQ(collider->getLocalScale(), glm::vec3(4));

  objectManager->stop();

  // The run must not have edited what the scene would save.
  EXPECT_EQ(collider->getLocalScale(), glm::vec3(1));
}

TEST(RuntimeObject, AnObjectAddedToAStoppedSceneStillAuthorsItsValues)
{
  const auto componentRegistry = std::make_shared<ComponentRegistry>();
  const auto objectManager = makeManager(componentRegistry);

  const auto [object, collider] = makeObjectWithCollider();
  objectManager->addObject(object);

  collider->setScale(glm::vec3(4));
  objectManager->start();

  // Authored before the run, so the run starts from it.
  EXPECT_EQ(collider->getLocalScale(), glm::vec3(4));
}

TEST(RuntimeObject, TheAddObjectEditStartsTheNewObjectInARunningScene)
{
  const auto componentRegistry = std::make_shared<ComponentRegistry>();
  const auto objectManager = makeManager(componentRegistry);

  objectManager->start();

  replication::applySceneEdit(*objectManager, replication::buildAddObject("Spawned"));

  ASSERT_EQ(objectManager->getObjects().size(), 1u);
  const auto object = objectManager->getObjects().front();

  replication::applySceneEdit(*objectManager,
                              replication::buildAddComponent(object->getUUID(), "Box"));

  const auto collider = object->getComponent<BoxCollider>(ComponentType::collider);
  ASSERT_NE(collider, nullptr);

  collider->setScale(glm::vec3(4));
  objectManager->stop();

  EXPECT_EQ(collider->getLocalScale(), glm::vec3(1));
}

TEST(RuntimeObject, InstantiatingIntoARunningSceneStartsTheWholeSubtree)
{
  const auto componentRegistry = std::make_shared<ComponentRegistry>();
  const auto authoring = makeManager(componentRegistry);

  // Author a parent with one child, each carrying a collider, then take the body the prefab path uses.
  const auto [parent, parentCollider] = makeObjectWithCollider();
  authoring->addObject(parent);

  const auto [child, childCollider] = makeObjectWithCollider();
  child->setParent(parent);
  authoring->addObject(child);

  const auto body = parent->serialize();

  const auto objectManager = makeManager(componentRegistry);
  objectManager->start();

  const auto instance = objectManager->instantiate(body);
  ASSERT_EQ(instance->getChildren().size(), 1u);

  const auto instanceCollider = instance->getComponent<BoxCollider>(ComponentType::collider);
  const auto instanceChildCollider =
    instance->getChildren().front()->getComponent<BoxCollider>(ComponentType::collider);

  ASSERT_NE(instanceCollider, nullptr);
  ASSERT_NE(instanceChildCollider, nullptr);

  instanceCollider->setScale(glm::vec3(4));
  instanceChildCollider->setScale(glm::vec3(4));

  objectManager->stop();

  // A child is registered by loadChildren rather than by the caller, so it is the node most likely to be
  // left inert if only the root is started.
  EXPECT_EQ(instanceCollider->getLocalScale(), glm::vec3(1));
  EXPECT_EQ(instanceChildCollider->getLocalScale(), glm::vec3(1));
}

TEST(RuntimeObject, UnpackingIntoARunningObjectAuthorsWhatItReads)
{
  const auto componentRegistry = std::make_shared<ComponentRegistry>();
  const auto authoring = makeManager(componentRegistry);

  const auto [source, sourceCollider] = makeObjectWithCollider();
  authoring->addObject(source);
  sourceCollider->setScale(glm::vec3(7));

  net::Message message(net::MessageType::objectSpawned);
  source->pack(message);

  const auto objectManager = makeManager(componentRegistry);
  objectManager->start();

  // The object is registered before it is read, so it is already running when unpack rebuilds it - the
  // case where writing through to the live value would strand the authored one at its default.
  const auto target = std::make_shared<Object>();
  objectManager->addObject(target);

  net::MessageReader reader(message);
  target->unpack(reader);

  const auto collider = target->getComponent<BoxCollider>(ComponentType::collider);
  ASSERT_NE(collider, nullptr);
  EXPECT_EQ(collider->getLocalScale(), glm::vec3(7));

  objectManager->stop();

  EXPECT_EQ(collider->getLocalScale(), glm::vec3(7));
}
