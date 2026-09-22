#include <gtest/gtest.h>

#include "ObjectManagerFixtures.h"
#include "TestScene.h"

#include "BindingContext.h"
#include "CameraBindings.h"
#include "InputState.h"
#include "InputUtilsBindings.h"
#include "RigidBodyBindings.h"
#include "TransformBindings.h"

#include "objects/Object.h"
#include "objects/components/Camera.h"
#include "objects/components/PlayerController.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"

#include <limits>
#include <string>
#include <uuid.h>

// Covers the property gaps filled in TransformBindings/RigidBodyBindings/CameraBindings: for each new
// getter/setter pair, the getter reads the live component value, the setter goes through the component's
// own setter rules (so a non-finite write is ignored the same way a script-driven edit through the
// existing bindings already is), and a component edit is recorded on BindingContext exactly when the
// component isn't covered by the per-tick state delta (RigidBody/Camera, not Transform - see
// AGENTS.md's Scripting paragraph).
namespace {
  using objectManagerFixtures::unknownUUID;

  class ScriptBindingGapsTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
      scene = fixtures::makeScene();
      BindingContext::setObjectManager(scene.objectManager.get());
    }

    void TearDown() override
    {
      // BindingContext's buffers/pointer are static, so a leftover from one test would otherwise leak
      // into the next.
      BindingContext::setObjectManager(nullptr);
      BindingContext::takeSpawned();
      BindingContext::takeDestroyed();
      BindingContext::takeComponentEdits();
    }

    fixtures::Scene scene;
  };

  std::string uuidOf(const std::shared_ptr<Object>& object)
  {
    return uuids::to_string(object->getUUID());
  }

  // --- Transform ---------------------------------------------------------------------------------

  TEST_F(ScriptBindingGapsTest, TransformGetLocalReadsLocalNotWorldValues)
  {
    const auto parent = fixtures::addObject(scene, "parent", glm::vec3(10, 0, 0));
    const auto child = fixtures::addChildObject(scene, "child", parent);
    fixtures::transformOf(child)->setPosition(glm::vec3(1, 2, 3));

    const auto bindings = TransformBindingsProvider::getBindings();
    const auto uuid = uuidOf(child);

    float x = 0, y = 0, z = 0;
    bindings.getLocalPosition(uuid.c_str(), &x, &y, &z);
    fixtures::expectNear("local position", glm::vec3(x, y, z), glm::vec3(1, 2, 3));

    // The world-space getter (already bound) combines the parent's position; local must not.
    bindings.getPosition(uuid.c_str(), &x, &y, &z);
    fixtures::expectNear("world position", glm::vec3(x, y, z), glm::vec3(11, 2, 3));
  }

  TEST_F(ScriptBindingGapsTest, TransformGetLocalScaleAndRotation)
  {
    const auto object = fixtures::addObject(scene, "object");
    fixtures::transformOf(object)->setScale(glm::vec3(2, 3, 4));
    fixtures::transformOf(object)->setRotation(glm::vec3(5, 6, 7));

    const auto bindings = TransformBindingsProvider::getBindings();
    const auto uuid = uuidOf(object);

    float x = 0, y = 0, z = 0;
    bindings.getLocalScale(uuid.c_str(), &x, &y, &z);
    fixtures::expectNear("local scale", glm::vec3(x, y, z), glm::vec3(2, 3, 4));

    bindings.getLocalRotation(uuid.c_str(), &x, &y, &z);
    fixtures::expectNear("local rotation", glm::vec3(x, y, z), glm::vec3(5, 6, 7));
  }

  TEST_F(ScriptBindingGapsTest, TransformSetPositionGoesThroughTheSetterRulesAndDoesNotRecordAnEdit)
  {
    const auto object = fixtures::addObject(scene, "object");
    const auto bindings = TransformBindingsProvider::getBindings();
    const auto uuid = uuidOf(object);

    bindings.setPosition(uuid.c_str(), 1.0f, 2.0f, 3.0f);
    fixtures::expectNear(fixtures::positionOf(object), glm::vec3(1, 2, 3));

    // Setter rule: Transform::setPosition ignores a non-finite write outright.
    bindings.setPosition(uuid.c_str(), std::numeric_limits<float>::infinity(), 0.0f, 0.0f);
    fixtures::expectNear(fixtures::positionOf(object), glm::vec3(1, 2, 3));

    // Negative control: Transform rides the per-tick state delta, so its setters (old or new) must never
    // buffer a component edit the way RigidBody/Camera's do.
    EXPECT_TRUE(BindingContext::takeComponentEdits().empty());
  }

  TEST_F(ScriptBindingGapsTest, TransformUnknownUUIDReadsNeutralDefaultAndChangesNothing)
  {
    const auto object = fixtures::addObject(scene, "object", glm::vec3(1, 2, 3));
    const auto bindings = TransformBindingsProvider::getBindings();
    const auto unknown = uuids::to_string(unknownUUID());

    float x = 5, y = 6, z = 7;
    bindings.getLocalPosition(unknown.c_str(), &x, &y, &z);
    // The binding writes nothing back when the uuid resolves to no Transform, leaving the caller's
    // (already-zeroed, by convention) buffer alone - it must not crash or touch any live object.
    EXPECT_FLOAT_EQ(x, 5.0f);
    EXPECT_FLOAT_EQ(y, 6.0f);
    EXPECT_FLOAT_EQ(z, 7.0f);

    bindings.setPosition(unknown.c_str(), 9.0f, 9.0f, 9.0f);
    fixtures::expectNear(fixtures::positionOf(object), glm::vec3(1, 2, 3));
  }

  // --- RigidBody -----------------------------------------------------------------------------------

  TEST_F(ScriptBindingGapsTest, RigidBodyGettersReadTheLiveComponent)
  {
    const auto object = fixtures::addObject(scene, "object");
    const auto rigidBody = fixtures::addRigidBody(object);
    rigidBody->setVelocity(glm::vec3(1, 2, 3));
    rigidBody->setAngularVelocity(glm::vec3(4, 5, 6));
    rigidBody->setMass(2.5f);
    rigidBody->setFriction(0.25f);
    rigidBody->setGravity(-3.0f);
    rigidBody->setDoGravity(false);

    const auto bindings = RigidBodyBindingsProvider::getBindings();
    const auto uuid = uuidOf(object);

    float x = 0, y = 0, z = 0;
    bindings.getVelocity(uuid.c_str(), &x, &y, &z);
    fixtures::expectNear("velocity", glm::vec3(x, y, z), glm::vec3(1, 2, 3));

    bindings.getAngularVelocity(uuid.c_str(), &x, &y, &z);
    fixtures::expectNear("angular velocity", glm::vec3(x, y, z), glm::vec3(4, 5, 6));

    EXPECT_FLOAT_EQ(bindings.getMass(uuid.c_str()), 2.5f);
    EXPECT_FLOAT_EQ(bindings.getFriction(uuid.c_str()), 0.25f);
    EXPECT_FLOAT_EQ(bindings.getGravity(uuid.c_str()), -3.0f);
    EXPECT_FALSE(bindings.getDoGravity(uuid.c_str()));
  }

  TEST_F(ScriptBindingGapsTest, RigidBodySetMassGoesThroughTheSetterFloorAndRecordsAnEdit)
  {
    const auto object = fixtures::addObject(scene, "object");
    const auto rigidBody = fixtures::addRigidBody(object);
    const auto bindings = RigidBodyBindingsProvider::getBindings();
    const auto uuid = uuidOf(object);

    bindings.setMass(uuid.c_str(), 5.0f);
    EXPECT_FLOAT_EQ(rigidBody->getMass(), 5.0f);

    // Setter rule: RigidBody::setMass floors to kMinMass rather than accepting a zero/negative mass.
    bindings.setMass(uuid.c_str(), -1.0f);
    EXPECT_GT(rigidBody->getMass(), 0.0f);

    // Positive control: not covered by the state delta, so the setter must buffer a replicated edit.
    const auto edits = BindingContext::takeComponentEdits();
    ASSERT_EQ(edits.size(), 1u);
    EXPECT_EQ(edits[0].first, object->getUUID());
    EXPECT_EQ(edits[0].second, rigidBody);
  }

  TEST_F(ScriptBindingGapsTest, RigidBodySettersRecordAnEditAndGettersDoNot)
  {
    const auto object = fixtures::addObject(scene, "object");
    fixtures::addRigidBody(object);
    const auto bindings = RigidBodyBindingsProvider::getBindings();
    const auto uuid = uuidOf(object);

    // Negative control: a read-only query must never buffer a replicated edit.
    bindings.getFriction(uuid.c_str());
    EXPECT_TRUE(BindingContext::takeComponentEdits().empty());

    // Three different setters on the same object's RigidBody still coalesce into the one edit
    // recordComponentEdit already dedupes by (uuid, component) - the drain packs current state either
    // way, so a broadcast per setter call would be redundant.
    bindings.setFriction(uuid.c_str(), 0.5f);
    bindings.setGravity(uuid.c_str(), -1.0f);
    bindings.setDoGravity(uuid.c_str(), false);
    EXPECT_EQ(BindingContext::takeComponentEdits().size(), 1u);
  }

  TEST_F(ScriptBindingGapsTest, RigidBodyUnknownUUIDReadsNeutralDefaultAndRecordsNoEdit)
  {
    const auto bindings = RigidBodyBindingsProvider::getBindings();
    const auto unknown = uuids::to_string(unknownUUID());

    float x = 0, y = 0, z = 0;
    bindings.getVelocity(unknown.c_str(), &x, &y, &z);
    fixtures::expectNear("velocity", glm::vec3(x, y, z), glm::vec3(0, 0, 0));

    EXPECT_FLOAT_EQ(bindings.getMass(unknown.c_str()), 0.0f);
    EXPECT_FALSE(bindings.getDoGravity(unknown.c_str()));

    bindings.setMass(unknown.c_str(), 42.0f);
    EXPECT_TRUE(BindingContext::takeComponentEdits().empty());
  }

  // --- Camera --------------------------------------------------------------------------------------

  TEST_F(ScriptBindingGapsTest, CameraGettersReadTheLiveComponent)
  {
    const auto object = fixtures::addObject(scene, "object");
    const auto camera = std::make_shared<Camera>();
    object->addComponent(camera);
    camera->setDirection(glm::vec3(1, 0, 0));
    camera->setFov(60.0f);
    camera->setNearPlane(0.5f);
    camera->setFarPlane(500.0f);
    camera->setActive(false);

    const auto bindings = CameraBindingsProvider::getBindings();
    const auto uuid = uuidOf(object);

    float x = 0, y = 0, z = 0;
    bindings.getDirection(uuid.c_str(), &x, &y, &z);
    fixtures::expectNear("direction", glm::vec3(x, y, z), glm::vec3(1, 0, 0));

    EXPECT_FLOAT_EQ(bindings.getFov(uuid.c_str()), 60.0f);
    EXPECT_FLOAT_EQ(bindings.getNearPlane(uuid.c_str()), 0.5f);
    EXPECT_FLOAT_EQ(bindings.getFarPlane(uuid.c_str()), 500.0f);
    EXPECT_FALSE(bindings.isActive(uuid.c_str()));
  }

  TEST_F(ScriptBindingGapsTest, CameraSettersGoThroughTheSetterRulesAndRecordAnEdit)
  {
    const auto object = fixtures::addObject(scene, "object");
    const auto camera = std::make_shared<Camera>();
    object->addComponent(camera);
    const auto bindings = CameraBindingsProvider::getBindings();
    const auto uuid = uuidOf(object);

    bindings.setFov(uuid.c_str(), 90.0f);
    EXPECT_FLOAT_EQ(camera->getFov(), 90.0f);

    // Setter rule: Camera::setFov ignores a non-finite write outright.
    bindings.setFov(uuid.c_str(), std::numeric_limits<float>::quiet_NaN());
    EXPECT_FLOAT_EQ(camera->getFov(), 90.0f);

    bindings.setDirection(uuid.c_str(), 0.0f, 1.0f, 0.0f);
    fixtures::expectNear("direction", camera->getDirection(), glm::vec3(0, 1, 0));

    bindings.setNearPlane(uuid.c_str(), 1.0f);
    bindings.setFarPlane(uuid.c_str(), 200.0f);
    bindings.setActive(uuid.c_str(), false);
    EXPECT_FLOAT_EQ(camera->getNearPlane(), 1.0f);
    EXPECT_FLOAT_EQ(camera->getFarPlane(), 200.0f);
    EXPECT_FALSE(camera->isActive());

    // Positive control: Camera isn't covered by the state delta, so every setter call above (six, one
    // non-finite no-op included since it still resolves the component) must have buffered exactly one
    // coalesced edit for this object - see BindingContext::recordComponentEdit's dedupe.
    const auto edits = BindingContext::takeComponentEdits();
    ASSERT_EQ(edits.size(), 1u);
    EXPECT_EQ(edits[0].first, object->getUUID());
    EXPECT_EQ(edits[0].second, camera);
  }

  TEST_F(ScriptBindingGapsTest, CameraUnknownUUIDReadsNeutralDefaultAndRecordsNoEdit)
  {
    const auto bindings = CameraBindingsProvider::getBindings();
    const auto unknown = uuids::to_string(unknownUUID());

    float x = 0, y = 0, z = 0;
    bindings.getDirection(unknown.c_str(), &x, &y, &z);
    fixtures::expectNear("direction", glm::vec3(x, y, z), glm::vec3(0, 0, -1));

    EXPECT_FLOAT_EQ(bindings.getFov(unknown.c_str()), 45.0f);
    EXPECT_FLOAT_EQ(bindings.getNearPlane(unknown.c_str()), 0.1f);
    EXPECT_FLOAT_EQ(bindings.getFarPlane(unknown.c_str()), 1000.0f);
    EXPECT_TRUE(bindings.isActive(unknown.c_str()));

    bindings.setFov(unknown.c_str(), 10.0f);
    EXPECT_TRUE(BindingContext::takeComponentEdits().empty());
  }

  // --- Input ---------------------------------------------------------------------------------------
  // InputUtilsBindings already covers everything InputState queries (see AGENTS.md's "Script binding
  // coverage" paragraph); this only proves an already-bound query still reads through cleanly so a
  // regression there does not slip past the audit unnoticed.

  TEST_F(ScriptBindingGapsTest, InputUtilsForObjectReadsThePlayerSlotInputState)
  {
    const auto object = fixtures::addObject(scene, "object");
    auto controller = std::make_shared<PlayerController>();
    object->addComponent(controller);
    controller->setPlayerSlot(3);

    InputState::setKeysPressed(3, { 65 });
    const auto bindings = InputUtilsBindingsProvider::getBindings();
    const auto uuid = uuidOf(object);

    EXPECT_TRUE(bindings.keyIsPressedForObject(uuid.c_str(), 65));
    EXPECT_FALSE(bindings.keyIsPressedForObject(uuid.c_str(), 66));

    InputState::removeSlot(3);
  }
}
