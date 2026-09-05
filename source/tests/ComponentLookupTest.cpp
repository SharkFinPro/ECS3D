#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Camera.h"
#include "objects/components/RigidBody.h"

#include <memory>

namespace {
  struct Family {
    std::shared_ptr<ComponentRegistry> componentRegistry = std::make_shared<ComponentRegistry>();
    std::unique_ptr<ObjectManager> objectManager;
    std::shared_ptr<Object> parent;
    std::shared_ptr<Object> child;
  };

  Family makeFamily()
  {
    Family family;
    registerDataComponents(*family.componentRegistry);
    family.objectManager = std::make_unique<ObjectManager>(family.componentRegistry);

    family.parent = std::make_shared<Object>("Parent");
    family.objectManager->addObject(family.parent);

    family.child = std::make_shared<Object>("Child");
    family.child->setParent(family.parent);
    family.objectManager->addObject(family.child);

    return family;
  }
}

TEST(ComponentLookup, GetComponentReachesTheParentForARigidBody)
{
  const auto family = makeFamily();
  family.parent->addComponent(std::make_shared<RigidBody>());

  // Deliberate: physics drives a child from the nearest ancestor's body.
  EXPECT_NE(family.child->getComponent<RigidBody>(ComponentType::rigidBody), nullptr);
}

TEST(ComponentLookup, GetComponentsReportsOnlyWhatTheObjectOwns)
{
  const auto family = makeFamily();
  family.parent->addComponent(std::make_shared<RigidBody>());

  // The distinction the editor's Add Component list turns on: asking what the child can reach would
  // hide Rigid Body on every child of an object that has one.
  EXPECT_FALSE(family.child->getComponents().contains(ComponentType::rigidBody));
  EXPECT_TRUE(family.parent->getComponents().contains(ComponentType::rigidBody));
}

TEST(ComponentLookup, NoOtherComponentTypeReachesThroughTheParent)
{
  const auto family = makeFamily();
  family.parent->addComponent(std::make_shared<Camera>());

  EXPECT_EQ(family.child->getComponent<Camera>(ComponentType::camera), nullptr);
}
