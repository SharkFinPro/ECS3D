#include <gtest/gtest.h>

#include "TestPrinters.h"
#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "Replication.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "scenes/SceneAsset.h"

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

  // Authored before the run, so the run starts from it. A characterization guard rather than a test of
  // the change: this held before addObject started anything, and has to keep holding.
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
  // case where writing through to the live value would strand the authored one at its default. This one
  // guards the stop/start bracket in unpack specifically: without addObject's start it would pass either
  // way, since an unstarted object authors what it reads regardless.
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

TEST(RuntimeObject, StoppingASceneRestoresTheAuthoredObjectTreeStructurally)
{
  const auto componentRegistry = std::make_shared<ComponentRegistry>();
  registerDataComponents(*componentRegistry);

  const auto scene = std::make_shared<SceneAsset>(
    uuids::uuid::from_string("11111111-1111-1111-1111-111111111111").value(), "Main", componentRegistry);
  auto& objectManager = *scene->getObjectManager();

  const auto parent = std::make_shared<Object>("Parent");
  objectManager.addObject(parent);

  const auto child = std::make_shared<Object>("Child");
  child->setParent(parent);
  objectManager.addObject(child);

  const auto sibling = std::make_shared<Object>("Sibling");
  objectManager.addObject(sibling);

  const auto toDestroy = std::make_shared<Object>("ToDestroy");
  objectManager.addObject(toDestroy);

  const auto parentUUID = parent->getUUID();
  const auto childUUID = child->getUUID();
  const auto siblingUUID = sibling->getUUID();
  const auto toDestroyUUID = toDestroy->getUUID();

  scene->start();

  // Simulate a script spawn: an object added at runtime that was never authored.
  const auto spawned = std::make_shared<Object>("Spawned");
  objectManager.addObject(spawned);
  const auto spawnedUUID = spawned->getUUID();

  // Simulate a script destroy of an authored object.
  objectManager.removeObject(toDestroy);
  objectManager.deleteObjectsMarkedForDeletion();

  // Simulate a runtime reparent: move Child from Parent to Sibling.
  parent->removeChild(child);
  child->setParent(sibling);
  sibling->addChild(child);

  scene->stop();

  // Positive checks: every authored object is back, with its authored uuid, name and parent.
  const auto restoredParent = objectManager.getObjectByUUID(parentUUID);
  const auto restoredChild = objectManager.getObjectByUUID(childUUID);
  const auto restoredSibling = objectManager.getObjectByUUID(siblingUUID);
  const auto restoredToDestroy = objectManager.getObjectByUUID(toDestroyUUID);

  ASSERT_NE(restoredParent, nullptr);
  ASSERT_NE(restoredChild, nullptr);
  ASSERT_NE(restoredSibling, nullptr);
  ASSERT_NE(restoredToDestroy, nullptr);

  EXPECT_EQ(restoredParent->getName(), "Parent");
  EXPECT_EQ(restoredChild->getName(), "Child");
  EXPECT_EQ(restoredSibling->getName(), "Sibling");
  EXPECT_EQ(restoredToDestroy->getName(), "ToDestroy");
  EXPECT_EQ(restoredChild->getParent(), restoredParent);
  EXPECT_TRUE(restoredSibling->getChildren().empty());

  // Negative check: the runtime spawn did not survive the stop.
  EXPECT_EQ(objectManager.getObjectByUUID(spawnedUUID), nullptr);
  EXPECT_EQ(objectManager.getAllObjects().size(), 4u);
}

TEST(RuntimeObject, StoppingASceneRestoresTransformValuesChangedDuringTheRun)
{
  const auto componentRegistry = std::make_shared<ComponentRegistry>();
  registerDataComponents(*componentRegistry);

  const auto scene = std::make_shared<SceneAsset>(
    uuids::uuid::from_string("22222222-2222-2222-2222-222222222222").value(), "Main", componentRegistry);
  auto& objectManager = *scene->getObjectManager();

  const auto object = std::make_shared<Object>("Object");
  objectManager.addObject(object);
  const auto objectUUID = object->getUUID();

  const auto transform = object->getComponent<Transform>(ComponentType::transform);
  ASSERT_NE(transform, nullptr);
  transform->setPosition(glm::vec3(1, 2, 3));

  scene->start();

  transform->setPosition(glm::vec3(9, 9, 9));
  EXPECT_EQ(transform->getLocalPosition(), glm::vec3(9, 9, 9));

  scene->stop();

  // The restore rebuilds every object from the authored capture, so the live value has to be read back
  // through the (possibly new) object the manager now holds, not the pointer captured before the run.
  const auto restored = objectManager.getObjectByUUID(objectUUID);
  ASSERT_NE(restored, nullptr);

  const auto restoredTransform = restored->getComponent<Transform>(ComponentType::transform);
  ASSERT_NE(restoredTransform, nullptr);
  EXPECT_EQ(restoredTransform->getLocalPosition(), glm::vec3(1, 2, 3));
}

TEST(RuntimeObject, RestoringWithoutOrAfterAlreadyConsumingACaptureIsANoOp)
{
  const auto componentRegistry = std::make_shared<ComponentRegistry>();
  registerDataComponents(*componentRegistry);

  const auto scene = std::make_shared<SceneAsset>(
    uuids::uuid::from_string("33333333-3333-3333-3333-333333333333").value(), "Main", componentRegistry);
  auto& objectManager = *scene->getObjectManager();

  const auto object = std::make_shared<Object>("Object");
  objectManager.addObject(object);

  // Stopping a scene that was never started has no capture to restore from: the object the manager
  // already holds must be left exactly as it is, not rebuilt from nothing.
  scene->stop();
  ASSERT_EQ(objectManager.getAllObjects().size(), 1u);
  EXPECT_EQ(objectManager.getObjects().front(), object);

  // Positive control: a real start/stop DOES rebuild the tree - proving the no-op above is not just a
  // scene that never restores anything.
  scene->start();

  const auto spawned = std::make_shared<Object>("Spawned");
  objectManager.addObject(spawned);
  ASSERT_EQ(objectManager.getAllObjects().size(), 2u);

  scene->stop();
  ASSERT_EQ(objectManager.getAllObjects().size(), 1u);
  EXPECT_EQ(objectManager.getObjectByUUID(spawned->getUUID()), nullptr);

  const auto afterFirstStop = objectManager.getObjects().front();

  // The capture was consumed by the stop above; a second stop with nothing left to restore from must not
  // rebuild the tree again.
  scene->stop();
  ASSERT_EQ(objectManager.getAllObjects().size(), 1u);
  EXPECT_EQ(objectManager.getObjects().front(), afterFirstStop);
}
