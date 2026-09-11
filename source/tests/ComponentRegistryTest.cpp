#include <gtest/gtest.h>

#include "TestScene.h"
#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "objects/components/Camera.h"
#include "objects/components/Component.h"
#include "objects/components/Transform.h"

#include <glm/vec3.hpp>
#include <memory>

TEST(ComponentRegistry, RegistersEveryDataComponentTypeUnderItsOwnName)
{
  ComponentRegistry registry;
  registerDataComponents(registry);

  // componentTypeToRegistryKey is the same name list registerDataComponents is written against
  // (Component.h's comment ties the two together), so it is the source of truth here rather than a
  // second, hand-copied list that could drift from the real one.
  for (const auto& [type, key] : componentTypeToRegistryKey)
  {
    EXPECT_TRUE(registry.isRegistered(key)) << key;

    const auto component = registry.create(key);
    ASSERT_NE(component, nullptr) << key;

    if (const auto parent = subComponentTypeToParent.find(type); parent != subComponentTypeToParent.end())
    {
      EXPECT_EQ(component->getSubType(), type) << key;
      EXPECT_EQ(component->getType(), parent->second) << key;
    }
    else
    {
      EXPECT_EQ(component->getType(), type) << key;
      EXPECT_EQ(component->getSubType(), ComponentType::SubComponentType_none) << key;
    }
  }
}

TEST(ComponentRegistry, UnknownTypeNameIsNotRegistered)
{
  ComponentRegistry registry;
  registerDataComponents(registry);

  EXPECT_FALSE(registry.isRegistered("NotARealComponent"));
  EXPECT_EQ(registry.create("NotARealComponent"), nullptr);

  // Positive control: the machinery that would report a real name as registered does fire.
  EXPECT_TRUE(registry.isRegistered("Transform"));
  EXPECT_NE(registry.create("Transform"), nullptr);
}

TEST(ComponentRegistry, AFreshRegistryKnowsNoDataComponents)
{
  const fixtures::Scene scene(fixtures::Components::none);

  EXPECT_FALSE(scene.componentRegistry->isRegistered("Transform"));
  EXPECT_FALSE(scene.componentRegistry->isRegistered("RigidBody"));
  EXPECT_EQ(scene.componentRegistry->create("Transform"), nullptr);

  // Positive control: a registry that does register the data components knows the same name.
  const auto registered = fixtures::makeScene();
  EXPECT_TRUE(registered.componentRegistry->isRegistered("Transform"));
}

TEST(ComponentRegistry, CreateCallsTheRegisteredFactory)
{
  ComponentRegistry registry;

  int invocations = 0;
  registry.registerComponent("Counted", [&invocations] {
    ++invocations;
    return std::make_shared<Transform>(glm::vec3(0), glm::vec3(1), glm::vec3(0));
  });

  const auto first = registry.create("Counted");
  const auto second = registry.create("Counted");

  EXPECT_EQ(invocations, 2);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);

  // Each create() is a fresh instance, not a shared singleton the factory handed out once.
  EXPECT_NE(first, second);
}

TEST(ComponentRegistry, RegisteringTheSameNameAgainReplacesTheFactory)
{
  ComponentRegistry registry;

  registry.registerComponent("Slot", [] { return std::make_shared<Transform>(glm::vec3(0), glm::vec3(1),
                                                                              glm::vec3(0)); });
  registry.registerComponent("Slot", [] { return std::make_shared<Camera>(); });

  // registerComponent assigns into the map by key, so the second registration under the same name wins
  // outright rather than being ignored as a first-registration-sticks policy.
  const auto component = registry.create("Slot");
  ASSERT_NE(component, nullptr);
  EXPECT_EQ(component->getType(), ComponentType::camera);
}

TEST(ComponentRegistry, TheTypeStringSerializeWritesResolvesBackThroughTheRegistry)
{
  ComponentRegistry registry;
  registerDataComponents(registry);

  // The same rule Object::loadFromJSON applies when reading a component back off disk: a Collider names
  // its shape in "subType", everything else names itself directly in "type".
  for (const auto& [type, key] : componentTypeToRegistryKey)
  {
    const auto component = registry.create(key);
    ASSERT_NE(component, nullptr) << key;

    const auto serialized = component->serialize();
    const auto writtenType = serialized.at("type").get<std::string>();
    const auto registryKey = writtenType == "Collider"
      ? serialized.at("subType").get<std::string>()
      : writtenType;

    const auto resolved = registry.create(registryKey);
    ASSERT_NE(resolved, nullptr) << key;
    EXPECT_EQ(resolved->getType(), component->getType()) << key;
    EXPECT_EQ(resolved->getSubType(), component->getSubType()) << key;
  }
}
