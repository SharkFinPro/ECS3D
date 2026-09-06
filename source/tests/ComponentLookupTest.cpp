#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Camera.h"
#include "objects/components/Component.h"
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

TEST(ComponentTypeTables, EveryPackedTypeCanBeBuiltAndNamed)
{
  const auto componentRegistry = std::make_shared<ComponentRegistry>();
  registerDataComponents(*componentRegistry);

  // componentTypeToRegistryKey is now two things: the factory lookup unpack uses, and the authority on
  // whether a discriminator off the wire names a component at all. A type added to one table and not the
  // other stops being a defect you notice - its edits are just dropped as malformed - so the tables are
  // pinned against each other and against the registry here.
  for (const auto& [type, key] : componentTypeToRegistryKey)
  {
    EXPECT_NE(componentRegistry->create(key), nullptr) << "no factory registered for " << key;
    EXPECT_TRUE(componentTypeToString.contains(type)) << "no display name for " << key;
  }

  EXPECT_EQ(componentTypeToRegistryKey.size(), componentTypeToString.size());

  // The parent collider type is deliberately in neither: colliders pack their shape, never this.
  EXPECT_FALSE(componentTypeToRegistryKey.contains(ComponentType::collider));
}

TEST(ComponentTypeTables, EveryBuiltComponentPacksTheTypeItWasBuiltFor)
{
  const auto componentRegistry = std::make_shared<ComponentRegistry>();
  registerDataComponents(*componentRegistry);

  // The other half of the pairing: a component's own getPackedType has to round-trip back to the key it
  // was created from, or an unpack that checks the discriminator against the component would reject an
  // edit the editor legitimately sent.
  for (const auto& [type, key] : componentTypeToRegistryKey)
  {
    const auto component = componentRegistry->create(key);
    ASSERT_NE(component, nullptr) << key;
    EXPECT_EQ(component->getPackedType(), type) << key;
  }
}
