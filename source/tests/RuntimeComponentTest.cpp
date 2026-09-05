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
