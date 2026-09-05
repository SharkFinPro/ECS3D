#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"

#include <memory>

// Proves the suite links and exercises the engine libraries without a window, GPU or server.

TEST(Smoke, DataComponentsAreRegistered)
{
  ComponentRegistry componentRegistry;
  registerDataComponents(componentRegistry);

  EXPECT_TRUE(componentRegistry.isRegistered("Transform"));
  EXPECT_NE(componentRegistry.create("Transform"), nullptr);

  EXPECT_FALSE(componentRegistry.isRegistered("NotAComponent"));
  EXPECT_EQ(componentRegistry.create("NotAComponent"), nullptr);
}

TEST(Smoke, ObjectManagerTracksAddedObjects)
{
  const auto componentRegistry = std::make_shared<ComponentRegistry>();
  registerDataComponents(*componentRegistry);

  ObjectManager objectManager(componentRegistry);
  EXPECT_TRUE(objectManager.getObjects().empty());

  const auto object = std::make_shared<Object>("Test Object");
  objectManager.addObject(object);

  EXPECT_FALSE(object->getUUID().is_nil());
  EXPECT_EQ(objectManager.getObjects().size(), 1u);
  EXPECT_EQ(objectManager.getObjectByUUID(object->getUUID()), object);
}
