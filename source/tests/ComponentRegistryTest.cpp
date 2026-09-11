#include <gtest/gtest.h>

#include "TestScene.h"
#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "objects/components/Component.h"
#include "objects/components/Transform.h"

#include <algorithm>
#include <glm/vec3.hpp>
#include <iterator>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

namespace {
  // Names present in `only` but absent from `all`. Both inputs must already be sorted.
  std::vector<std::string> namesMissingFrom(const std::vector<std::string>& all, const std::vector<std::string>& only)
  {
    std::vector<std::string> missing;
    std::ranges::set_difference(only, all, std::back_inserter(missing));
    return missing;
  }

  std::string joinNames(const std::vector<std::string>& names)
  {
    std::string joined;
    for (const auto& name : names)
    {
      if (!joined.empty())
      {
        joined += ", ";
      }
      joined += name;
    }
    return joined;
  }
}

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

TEST(ComponentRegistry, TheTypeStringSerializeWritesResolvesBackThroughTheRegistry)
{
  ComponentRegistry registry;
  registerDataComponents(registry);

  // The same rule Object::loadFromJSON applies to the "components" array: a Collider names its shape in
  // "subType", everything else names itself directly in "type". Scripts are excluded: loadFromJSON reads
  // them from a separate "scripts" array and always creates "Script" without consulting any type field.
  for (const auto& [type, key] : componentTypeToRegistryKey)
  {
    if (type == ComponentType::script)
    {
      continue;
    }

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

TEST(ComponentRegistry, RegisteredNamesMatchTheWireTypeTableExactly)
{
  // componentTypeToRegistryKey is Object::unpack's only route from a wire ComponentType back to a
  // ComponentRegistry factory name. A name registered here but absent from that table builds, saves and
  // loads through JSON without ever failing, then breaks silently the first time it is reconstructed off
  // the wire; a name in that table but never registered fails every unpack immediately. Neither direction
  // is covered by RegistersEveryDataComponentTypeUnderItsOwnName above, which only ever walks the wire
  // table forward and so cannot see a registered name the table omits.
  ComponentRegistry registry;
  registerDataComponents(registry);

  std::set<std::string> wireNameSet;
  for (const auto& [type, key] : componentTypeToRegistryKey)
  {
    wireNameSet.insert(key);
  }
  const std::vector<std::string> wireNames(wireNameSet.begin(), wireNameSet.end());

  const auto registeredNames = registry.registeredNames();

  const auto registeredButNotOnWire = namesMissingFrom(wireNames, registeredNames);
  const auto onWireButNeverRegistered = namesMissingFrom(registeredNames, wireNames);

  EXPECT_TRUE(registeredButNotOnWire.empty())
    << "registered in registerDataComponents but missing from componentTypeToRegistryKey: "
    << joinNames(registeredButNotOnWire);
  EXPECT_TRUE(onWireButNeverRegistered.empty())
    << "named in componentTypeToRegistryKey but never registered by registerDataComponents: "
    << joinNames(onWireButNeverRegistered);
}

TEST(ComponentRegistry, RegisteredNamesMatchTheWireTypeTableComparisonCatchesAnExtraName)
{
  // Positive control for the test above: prove the comparison actually fails when the two sides diverge,
  // rather than passing vacuously. An extra name registered outside registerDataComponents must show up
  // as exactly the one name the wire table doesn't know about.
  ComponentRegistry registry;
  registerDataComponents(registry);
  registry.registerComponent("NotOnTheWireTable", [] { return std::make_shared<Transform>(); });

  std::set<std::string> wireNameSet;
  for (const auto& [type, key] : componentTypeToRegistryKey)
  {
    wireNameSet.insert(key);
  }
  const std::vector<std::string> wireNames(wireNameSet.begin(), wireNameSet.end());

  const auto registeredButNotOnWire = namesMissingFrom(wireNames, registry.registeredNames());

  ASSERT_EQ(registeredButNotOnWire.size(), 1);
  EXPECT_EQ(registeredButNotOnWire.front(), "NotOnTheWireTable");
}
