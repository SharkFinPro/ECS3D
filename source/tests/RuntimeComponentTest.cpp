#include <gtest/gtest.h>

#include "TestPrinters.h"
#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "Replication.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/collisions/BoxCollider.h"

#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <memory>

namespace {
  std::shared_ptr<Object> makeObject(ObjectManager& objectManager)
  {
    auto object = std::make_shared<Object>("Object");
    objectManager.addObject(object);

    return object;
  }

  std::unique_ptr<ObjectManager> makeManager(const std::shared_ptr<ComponentRegistry>& componentRegistry)
  {
    registerDataComponents(*componentRegistry);

    return std::make_unique<ObjectManager>(componentRegistry);
  }
}

TEST(RuntimeComponent, AComponentAddedWhileStoppedAuthorsItsValues)
{
  const auto componentRegistry = std::make_shared<ComponentRegistry>();
  const auto objectManager = makeManager(componentRegistry);
  const auto object = makeObject(*objectManager);

  const auto collider = std::make_shared<BoxCollider>();
  object->addComponent(collider);

  collider->setScale(glm::vec3(4));
  object->start();

  // Authored before the run, so the run starts from it.
  EXPECT_EQ(collider->getLocalScale(), glm::vec3(4));
}

TEST(RuntimeComponent, AComponentAddedWhileRunningWritesToItsLiveValue)
{
  const auto componentRegistry = std::make_shared<ComponentRegistry>();
  const auto objectManager = makeManager(componentRegistry);
  const auto object = makeObject(*objectManager);

  object->start();

  const auto collider = std::make_shared<BoxCollider>();
  object->addComponent(collider);

  collider->setScale(glm::vec3(4));

  EXPECT_EQ(collider->getLocalScale(), glm::vec3(4));

  object->stop();

  // The run must not have edited what the scene would save.
  EXPECT_EQ(collider->getLocalScale(), glm::vec3(1));
}

TEST(RuntimeComponent, TheAddComponentEditStartsTheComponentInARunningScene)
{
  const auto componentRegistry = std::make_shared<ComponentRegistry>();
  const auto objectManager = makeManager(componentRegistry);
  const auto object = makeObject(*objectManager);

  object->start();

  replication::applySceneEdit(*objectManager, replication::buildAddComponent(object->getUUID(), "Box"));

  const auto collider = object->getComponent<BoxCollider>(ComponentType::collider);
  ASSERT_NE(collider, nullptr);

  collider->setScale(glm::vec3(4));
  object->stop();

  EXPECT_EQ(collider->getLocalScale(), glm::vec3(1));
}

TEST(RuntimeComponent, WhatTheSceneWouldSaveIsUntouchedByTheRun)
{
  const auto componentRegistry = std::make_shared<ComponentRegistry>();
  const auto objectManager = makeManager(componentRegistry);
  const auto object = makeObject(*objectManager);

  // Started through the manager, which is how a scene starts.
  objectManager->start();

  const auto collider = std::make_shared<BoxCollider>();
  object->addComponent(collider);
  collider->setScale(glm::vec3(4));

  bool sawTheCollider = false;

  // Held in a named value: iterating object->serialize().at(...) directly walks a reference into a
  // temporary that gcc does not keep alive for the loop.
  const auto serialized = object->serialize();

  for (const auto& component : serialized.at("components"))
  {
    if (component.at("type") != "Collider")
    {
      continue;
    }

    sawTheCollider = true;

    // The symptom the whole change is about: a runtime edit must not end up in the saved scene.
    EXPECT_FLOAT_EQ(component.at("scale").at(0).get<float>(), 1.0f);
    EXPECT_FLOAT_EQ(component.at("scale").at(1).get<float>(), 1.0f);
    EXPECT_FLOAT_EQ(component.at("scale").at(2).get<float>(), 1.0f);
  }

  EXPECT_TRUE(sawTheCollider);
}

TEST(RuntimeComponent, StartingAnAlreadyRunningObjectKeepsItsLiveValues)
{
  const auto componentRegistry = std::make_shared<ComponentRegistry>();
  const auto objectManager = makeManager(componentRegistry);
  const auto object = makeObject(*objectManager);

  object->start();

  const auto collider = std::make_shared<BoxCollider>();
  object->addComponent(collider);
  collider->setScale(glm::vec3(4));

  // A second start would otherwise re-seed every live value from the authored one.
  object->start();

  EXPECT_EQ(collider->getLocalScale(), glm::vec3(4));
}

TEST(RuntimeComponent, ASecondComponentOfTheSameTypeIsNotAdopted)
{
  const auto componentRegistry = std::make_shared<ComponentRegistry>();
  const auto objectManager = makeManager(componentRegistry);
  const auto object = makeObject(*objectManager);

  object->start();

  const auto first = std::make_shared<BoxCollider>();
  object->addComponent(first);

  const auto second = std::make_shared<BoxCollider>();
  object->addComponent(second);

  EXPECT_EQ(object->getComponent<BoxCollider>(ComponentType::collider), first);

  // The rejected one must not be left owned or started, or it would outlive the call as a live orphan.
  EXPECT_EQ(second->getOwner(), nullptr);

  second->setScale(glm::vec3(4));
  EXPECT_EQ(second->getLocalScale(), glm::vec3(4));

  object->stop();

  EXPECT_EQ(second->getLocalScale(), glm::vec3(4));
}

TEST(RuntimeComponent, StoppingAnObjectLetsALaterComponentAuthorAgain)
{
  const auto componentRegistry = std::make_shared<ComponentRegistry>();
  const auto objectManager = makeManager(componentRegistry);
  const auto object = makeObject(*objectManager);

  object->start();
  object->stop();

  const auto collider = std::make_shared<BoxCollider>();
  object->addComponent(collider);

  collider->setScale(glm::vec3(4));

  EXPECT_EQ(collider->getLocalScale(), glm::vec3(4));
}

TEST(RuntimeComponent, RemovingAComponentFromARunningObjectStopsIt)
{
  const auto componentRegistry = std::make_shared<ComponentRegistry>();
  const auto objectManager = makeManager(componentRegistry);
  const auto removedFrom = makeObject(*objectManager);
  const auto keptOn = makeObject(*objectManager);
  const auto neverStarted = makeObject(*objectManager);

  removedFrom->start();
  keptOn->start();

  const auto removedCollider = std::make_shared<BoxCollider>();
  removedFrom->addComponent(removedCollider);
  removedCollider->setScale(glm::vec3(4));

  const auto keptCollider = std::make_shared<BoxCollider>();
  keptOn->addComponent(keptCollider);
  keptCollider->setScale(glm::vec3(4));

  // Started independently of the object, so its ComponentVariable goes live without neverStarted
  // itself being started - isolating removeComponent's own m_started check from the component's.
  const auto unstartedObjectsCollider = std::make_shared<BoxCollider>();
  neverStarted->addComponent(unstartedObjectsCollider);
  unstartedObjectsCollider->start();
  unstartedObjectsCollider->setScale(glm::vec3(4));

  removedFrom->removeComponent(removedCollider);
  neverStarted->removeComponent(unstartedObjectsCollider);

  // Removed from a running object: stop() ran, reverting the live value to what was authored - the
  // same reset Object::stop() itself produces for a component that stays on the object.
  EXPECT_EQ(removedCollider->getLocalScale(), glm::vec3(1));

  // Positive control: the same edit, made to a component that stays held on its (also running) object,
  // keeps its runtime value - removal, not merely running, is what triggers the reset.
  EXPECT_EQ(keptCollider->getLocalScale(), glm::vec3(4));

  // Contrast: neverStarted's own m_started is false, so removing from it must not stop the component -
  // its live value (set on the component directly, above) is left alone.
  EXPECT_EQ(unstartedObjectsCollider->getLocalScale(), glm::vec3(4));
}

TEST(RuntimeComponent, RemovingAComponentThatIsNotHeldDoesNotStopOrEvictTheHeldOne)
{
  const auto componentRegistry = std::make_shared<ComponentRegistry>();
  const auto objectManager = makeManager(componentRegistry);
  const auto object = makeObject(*objectManager);

  object->start();

  const auto held = std::make_shared<BoxCollider>();
  object->addComponent(held);
  held->setScale(glm::vec3(4));

  // Never added to the object, so it is not the component in its collider slot - same type, different
  // instance.
  const auto foreign = std::make_shared<BoxCollider>();

  object->removeComponent(foreign);

  // The held collider must still be the one in the object's slot, and untouched by the foreign removal.
  EXPECT_EQ(object->getComponent<BoxCollider>(ComponentType::collider), held);
  EXPECT_EQ(held->getLocalScale(), glm::vec3(4));
}
