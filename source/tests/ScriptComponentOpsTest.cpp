#include <gtest/gtest.h>

#include "ComponentOpsBindings.h"
#include "BindingContext.h"
#include "TestScene.h"
#include "ObjectManagerFixtures.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/RigidBody.h"
#include "objects/components/collisions/BoxCollider.h"

#include <glm/vec3.hpp>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <uuid.h>

namespace {
  // Points BindingContext at a real, freshly built ObjectManager for the duration of the test - the same
  // way ScriptSystem points it at the server's current scene each tick - and clears every static buffer
  // afterward so a later test in the suite never inherits this one's pointer or a leftover flag.
  class ScriptComponentOpsTest : public ::testing::Test {
  protected:
    fixtures::Scene scene = fixtures::makeScene();

    void SetUp() override
    {
      BindingContext::setObjectManager(scene.objectManager.get());
    }

    void TearDown() override
    {
      BindingContext::setObjectManager(nullptr);
      (void)BindingContext::takeStructuralComponentChange();
      (void)BindingContext::takeSpawned();
      (void)BindingContext::takeDestroyed();
      (void)BindingContext::takeComponentEdits();
    }
  };

  ComponentOpsBindings bindings()
  {
    return ComponentOpsBindingsProvider::getBindings();
  }

  bool has(const std::shared_ptr<Object>& object, const char* type)
  {
    const auto uuid = uuids::to_string(object->getUUID());
    return bindings().hasComponent(uuid.c_str(), type);
  }

  bool add(const std::shared_ptr<Object>& object, const char* type)
  {
    const auto uuid = uuids::to_string(object->getUUID());
    return bindings().addComponent(uuid.c_str(), type);
  }

  bool remove(const std::shared_ptr<Object>& object, const char* type)
  {
    const auto uuid = uuids::to_string(object->getUUID());
    return bindings().removeComponent(uuid.c_str(), type);
  }

  std::set<std::string> types(const std::shared_ptr<Object>& object)
  {
    const auto uuid = uuids::to_string(object->getUUID());
    const std::string raw = bindings().getComponentTypes(uuid.c_str());

    std::set<std::string> result;
    std::stringstream stream(raw);
    std::string entry;
    while (std::getline(stream, entry, ','))
    {
      if (!entry.empty())
      {
        result.insert(entry);
      }
    }

    return result;
  }
}

TEST_F(ScriptComponentOpsTest, HasComponentIsTrueForPresentAndFalseForAbsent)
{
  const auto object = fixtures::addObject(scene, "Object");

  EXPECT_TRUE(has(object, "Transform"));
  EXPECT_FALSE(has(object, "RigidBody"));
}

TEST_F(ScriptComponentOpsTest, AddSucceedsAndTheComponentExists)
{
  const auto object = fixtures::addObject(scene, "Object");

  EXPECT_TRUE(add(object, "RigidBody"));
  EXPECT_TRUE(has(object, "RigidBody"));
  EXPECT_NE(object->getComponent<RigidBody>(ComponentType::rigidBody), nullptr);
}

// Positive control: a component added while the object is running is started, matching what
// RuntimeComponentTest proves for Object::addComponent directly - setting a live value after start writes
// through the live slot, so it survives a stop/start that would otherwise re-seed it from the authored one.
TEST_F(ScriptComponentOpsTest, AddedComponentIsStartedWhenTheObjectIsRunning)
{
  const auto object = fixtures::addObject(scene, "Object");
  object->start();

  ASSERT_TRUE(add(object, "Box"));
  const auto collider = object->getComponent<BoxCollider>(ComponentType::collider);
  ASSERT_NE(collider, nullptr);

  collider->setScale(glm::vec3(4));
  object->stop();

  // If start() had not run, setScale would have written straight to the authored value instead, and this
  // would read back 4 rather than the untouched default.
  EXPECT_EQ(collider->getLocalScale(), glm::vec3(1));
}

// Negative control for the test above: the same component added to a stopped object is not started, so a
// value set on it writes into the authored slot directly and is still there once the object does start.
TEST_F(ScriptComponentOpsTest, AddedComponentIsNotStartedWhenTheObjectIsStopped)
{
  const auto object = fixtures::addObject(scene, "Object");

  ASSERT_TRUE(add(object, "Box"));
  const auto collider = object->getComponent<BoxCollider>(ComponentType::collider);
  ASSERT_NE(collider, nullptr);

  collider->setScale(glm::vec3(4));
  object->start();

  EXPECT_EQ(collider->getLocalScale(), glm::vec3(4));
}

TEST_F(ScriptComponentOpsTest, AddRefusesADuplicateOfAUniqueType)
{
  const auto object = fixtures::addObject(scene, "Object");

  ASSERT_TRUE(add(object, "RigidBody"));
  const auto first = object->getComponent<RigidBody>(ComponentType::rigidBody);
  ASSERT_NE(first, nullptr);

  EXPECT_FALSE(add(object, "RigidBody"));
  EXPECT_EQ(object->getComponent<RigidBody>(ComponentType::rigidBody), first);
}

TEST_F(ScriptComponentOpsTest, AddAndRemoveBothRefuseTransform)
{
  const auto object = fixtures::addObject(scene, "Object");

  EXPECT_FALSE(add(object, "Transform"));
  EXPECT_FALSE(remove(object, "Transform"));
  EXPECT_TRUE(has(object, "Transform"));
}

TEST_F(ScriptComponentOpsTest, EveryOperationRefusesAnUnknownName)
{
  const auto object = fixtures::addObject(scene, "Object");

  EXPECT_FALSE(has(object, "NotARealComponent"));
  EXPECT_FALSE(add(object, "NotARealComponent"));
  EXPECT_FALSE(remove(object, "NotARealComponent"));
}

TEST_F(ScriptComponentOpsTest, RemoveSucceedsWhenTheComponentIsPresent)
{
  const auto object = fixtures::addObject(scene, "Object");
  ASSERT_TRUE(add(object, "RigidBody"));

  EXPECT_TRUE(remove(object, "RigidBody"));
  EXPECT_FALSE(has(object, "RigidBody"));
  EXPECT_EQ(object->getComponent<RigidBody>(ComponentType::rigidBody), nullptr);
}

TEST_F(ScriptComponentOpsTest, RemoveRefusesWhenTheComponentIsAbsent)
{
  const auto object = fixtures::addObject(scene, "Object");

  EXPECT_FALSE(remove(object, "RigidBody"));
}

TEST_F(ScriptComponentOpsTest, GetComponentTypesListsExactlyWhatIsPresent)
{
  const auto object = fixtures::addObject(scene, "Object");
  ASSERT_TRUE(add(object, "RigidBody"));
  ASSERT_TRUE(add(object, "Box"));

  const std::set<std::string> expected = { "Transform", "RigidBody", "Box" };
  EXPECT_EQ(types(object), expected);
}

TEST_F(ScriptComponentOpsTest, GetComponentTypesExcludesScript)
{
  const auto object = fixtures::addObject(scene, "Object");

  // Script lives in the object's own script list, not its component map, and this API does not add one
  // (it needs a class name) - the newly built object carries only its Transform.
  const std::set<std::string> expected = { "Transform" };
  EXPECT_EQ(types(object), expected);
}

TEST_F(ScriptComponentOpsTest, AMalformedOrUnknownUUIDIsSafeEverywhere)
{
  const auto ops = bindings();

  EXPECT_FALSE(ops.hasComponent("not-a-uuid", "Transform"));
  EXPECT_FALSE(ops.addComponent("not-a-uuid", "RigidBody"));
  EXPECT_FALSE(ops.removeComponent("not-a-uuid", "Transform"));
  EXPECT_STREQ(ops.getComponentTypes("not-a-uuid"), "");

  const auto unknown = uuids::to_string(objectManagerFixtures::unknownUUID());
  EXPECT_FALSE(ops.hasComponent(unknown.c_str(), "Transform"));
  EXPECT_FALSE(ops.addComponent(unknown.c_str(), "RigidBody"));
  EXPECT_FALSE(ops.removeComponent(unknown.c_str(), "Transform"));
  EXPECT_STREQ(ops.getComponentTypes(unknown.c_str()), "");
}

TEST_F(ScriptComponentOpsTest, AnAppliedAddOrRemoveFlagsAStructuralChangeForReplication)
{
  const auto object = fixtures::addObject(scene, "Object");

  // Clear whatever the fixture's own setup left pending, so the assertions below see only this test's ops.
  (void)BindingContext::takeStructuralComponentChange();

  ASSERT_TRUE(add(object, "RigidBody"));
  EXPECT_TRUE(BindingContext::takeStructuralComponentChange());
  // Consuming it clears the flag - a second read with nothing new since must come back false.
  EXPECT_FALSE(BindingContext::takeStructuralComponentChange());

  ASSERT_TRUE(remove(object, "RigidBody"));
  EXPECT_TRUE(BindingContext::takeStructuralComponentChange());
}

TEST_F(ScriptComponentOpsTest, ARefusedAddOrRemoveDoesNotFlagAStructuralChange)
{
  const auto object = fixtures::addObject(scene, "Object");
  (void)BindingContext::takeStructuralComponentChange();

  EXPECT_FALSE(add(object, "Transform"));
  EXPECT_FALSE(remove(object, "RigidBody"));

  EXPECT_FALSE(BindingContext::takeStructuralComponentChange());
}
