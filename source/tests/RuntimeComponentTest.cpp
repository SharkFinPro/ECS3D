#include <gtest/gtest.h>

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
