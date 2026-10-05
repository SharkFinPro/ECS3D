#include <gtest/gtest.h>

#include "ObjectManagerFixtures.h"
#include "TestScene.h"

#include "BindingContext.h"
#include "RigidBodyBindings.h"
#include "TransformBindings.h"

#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"

#include <glm/vec3.hpp>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <uuid.h>
#include <vector>

namespace {
  using objectManagerFixtures::unknownUUID;

  constexpr float inf = std::numeric_limits<float>::infinity();

  class TransformRigidBodyBindingsTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
      scene = fixtures::makeScene();
      BindingContext::setObjectManager(scene.objectManager.get());
    }

    void TearDown() override
    {
      BindingContext::setObjectManager(nullptr);
      BindingContext::setAssetRegistry(nullptr);
      BindingContext::setRaycast(nullptr);
      BindingContext::setOverlapSphere(nullptr);
      BindingContext::takeSpawned();
      BindingContext::takeDestroyed();
      BindingContext::takeComponentEdits();
      BindingContext::takeStructuralComponentChanges();
    }

    fixtures::Scene scene;
    TransformBindings transform = TransformBindingsProvider::getBindings();
    RigidBodyBindings rigidBody = RigidBodyBindingsProvider::getBindings();
  };

  std::string uuidOf(const std::shared_ptr<Object>& object)
  {
    return uuids::to_string(object->getUUID());
  }

  glm::vec3 read(void (*getter)(const char*, float*, float*, float*), const std::string& uuid)
  {
    float x = 0, y = 0, z = 0;
    getter(uuid.c_str(), &x, &y, &z);
    return { x, y, z };
  }

  // --- Transform -------------------------------------------------------------------------------------

  TEST_F(TransformRigidBodyBindingsTest, TransformWorldGettersCombineWithTheParent)
  {
    const auto parent = fixtures::addObject(scene, "parent", glm::vec3(10, 0, 0), glm::vec3(2, 2, 2));
    const auto child = fixtures::addChildObject(scene, "child", parent);
    fixtures::transformOf(child)->setScale(glm::vec3(3, 3, 3));
    fixtures::transformOf(child)->setRotation(glm::vec3(0, 30, 0));
    const auto uuid = uuidOf(child);

    fixtures::expectNear("world scale", read(transform.getScale, uuid), glm::vec3(6, 6, 6), 1e-4f);
    fixtures::expectNear("world rotation", read(transform.getRotation, uuid), glm::vec3(0, 30, 0), 1e-3f);

    // The local getter of the same object differs, so the world one really did combine.
    fixtures::expectNear("local scale", read(transform.getLocalScale, uuid), glm::vec3(3, 3, 3));
  }

  TEST_F(TransformRigidBodyBindingsTest, TransformSetScaleAndRotationWriteTheLocalValues)
  {
    const auto object = fixtures::addObject(scene, "object");
    const auto uuid = uuidOf(object);

    transform.setScale(uuid.c_str(), 2.0f, 3.0f, 4.0f);
    transform.setRotation(uuid.c_str(), 10.0f, 20.0f, 30.0f);
    fixtures::expectNear("scale", read(transform.getLocalScale, uuid), glm::vec3(2, 3, 4));
    fixtures::expectNear("rotation", read(transform.getLocalRotation, uuid), glm::vec3(10, 20, 30));

    transform.setScale(uuid.c_str(), inf, 1.0f, 1.0f);
    transform.setRotation(uuid.c_str(), 0.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f);
    fixtures::expectNear("scale kept", read(transform.getLocalScale, uuid), glm::vec3(2, 3, 4));
    fixtures::expectNear("rotation kept", read(transform.getLocalRotation, uuid), glm::vec3(10, 20, 30));

    // Transform rides the state delta, so none of these buffer a component edit.
    EXPECT_TRUE(BindingContext::takeComponentEdits().empty());
  }

  TEST_F(TransformRigidBodyBindingsTest, TransformMoveAddsToThePositionAndIgnoresNonFiniteSteps)
  {
    const auto object = fixtures::addObject(scene, "object", glm::vec3(1, 2, 3));
    const auto uuid = uuidOf(object);

    transform.move(uuid.c_str(), 1.0f, 1.0f, 1.0f);
    fixtures::expectNear(fixtures::positionOf(object), glm::vec3(2, 3, 4));

    transform.move(uuid.c_str(), inf, 0.0f, 0.0f);
    fixtures::expectNear(fixtures::positionOf(object), glm::vec3(2, 3, 4));
  }

  TEST_F(TransformRigidBodyBindingsTest, TransformStartAndStopSwapBetweenLiveAndAuthoredValues)
  {
    const auto object = fixtures::addObject(scene, "object", glm::vec3(1, 2, 3));
    const auto uuid = uuidOf(object);

    transform.start(uuid.c_str());
    transform.move(uuid.c_str(), 5.0f, 0.0f, 0.0f);
    fixtures::expectNear("live", fixtures::positionOf(object), glm::vec3(6, 2, 3));

    transform.stop(uuid.c_str());
    fixtures::expectNear("authored", fixtures::positionOf(object), glm::vec3(1, 2, 3));
  }

  TEST_F(TransformRigidBodyBindingsTest, TransformHasAndUnknownTargetsChangeNothing)
  {
    const auto object = fixtures::addObject(scene, "object", glm::vec3(1, 2, 3));
    const auto uuid = uuidOf(object);
    const auto unknown = uuids::to_string(unknownUUID());

    EXPECT_TRUE(transform.has(uuid.c_str()));
    EXPECT_FALSE(transform.has(unknown.c_str()));
    EXPECT_FALSE(transform.has("not-a-uuid"));
    EXPECT_FALSE(transform.has(nullptr));

    transform.setScale(unknown.c_str(), 9, 9, 9);
    transform.setRotation(unknown.c_str(), 9, 9, 9);
    transform.move(unknown.c_str(), 9, 9, 9);
    transform.start(unknown.c_str());
    transform.stop(unknown.c_str());
    transform.setScale(nullptr, 9, 9, 9);
    fixtures::expectNear(fixtures::positionOf(object), glm::vec3(1, 2, 3));
    fixtures::expectNear("scale", read(transform.getLocalScale, uuid), glm::vec3(1, 1, 1));

    float x = 5, y = 6, z = 7;
    transform.getScale(unknown.c_str(), &x, &y, &z);
    transform.getRotation(unknown.c_str(), &x, &y, &z);
    transform.getPosition(unknown.c_str(), &x, &y, &z);
    fixtures::expectNear(glm::vec3(x, y, z), glm::vec3(5, 6, 7));

    // Positive control: the live uuid is acted on.
    transform.setScale(uuid.c_str(), 9, 9, 9);
    fixtures::expectNear("scale", read(transform.getLocalScale, uuid), glm::vec3(9, 9, 9));
  }

  // --- RigidBody -------------------------------------------------------------------------------------

  TEST_F(TransformRigidBodyBindingsTest, RigidBodySetVelocitiesWriteTheComponentAndIgnoreNonFinite)
  {
    const auto object = fixtures::addObject(scene, "object");
    const auto body = fixtures::addRigidBody(object);
    const auto uuid = uuidOf(object);

    rigidBody.setVelocity(uuid.c_str(), 1.0f, 2.0f, 3.0f);
    rigidBody.setAngularVelocity(uuid.c_str(), 4.0f, 5.0f, 6.0f);
    fixtures::expectNear("velocity", body->getVelocity(), glm::vec3(1, 2, 3));
    fixtures::expectNear("angular velocity", body->getAngularVelocity(), glm::vec3(4, 5, 6));

    rigidBody.setVelocity(uuid.c_str(), inf, 0.0f, 0.0f);
    rigidBody.setAngularVelocity(uuid.c_str(), 0.0f, inf, 0.0f);
    fixtures::expectNear("velocity kept", body->getVelocity(), glm::vec3(1, 2, 3));
    fixtures::expectNear("angular velocity kept", body->getAngularVelocity(), glm::vec3(4, 5, 6));

    EXPECT_TRUE(BindingContext::takeComponentEdits().empty());
  }

  TEST_F(TransformRigidBodyBindingsTest, RigidBodyIsFallingAndHasReadTheLiveObject)
  {
    const auto object = fixtures::addObject(scene, "object");
    const auto body = fixtures::addRigidBody(object);
    const auto bare = fixtures::addObject(scene, "bare");
    const auto uuid = uuidOf(object);

    body->setFalling(false);
    EXPECT_FALSE(rigidBody.isFalling(uuid.c_str()));
    body->setFalling(true);
    EXPECT_TRUE(rigidBody.isFalling(uuid.c_str()));

    EXPECT_TRUE(rigidBody.has(uuid.c_str()));
    EXPECT_FALSE(rigidBody.has(uuidOf(bare).c_str()));
    EXPECT_FALSE(rigidBody.has(uuids::to_string(unknownUUID()).c_str()));
    EXPECT_FALSE(rigidBody.has("not-a-uuid"));
    EXPECT_FALSE(rigidBody.has(nullptr));
    EXPECT_FALSE(rigidBody.isFalling(uuidOf(bare).c_str()));
  }

  TEST_F(TransformRigidBodyBindingsTest, RigidBodyAngularVelocityMissLeavesTheCallerBufferAlone)
  {
    const auto unknown = uuids::to_string(unknownUUID());

    float x = 5, y = 6, z = 7;
    rigidBody.getAngularVelocity(unknown.c_str(), &x, &y, &z);
    fixtures::expectNear(glm::vec3(x, y, z), glm::vec3(5, 6, 7));

    const auto object = fixtures::addObject(scene, "object");
    fixtures::addRigidBody(object)->setAngularVelocity(glm::vec3(1, 2, 3));
    rigidBody.getAngularVelocity(uuidOf(object).c_str(), &x, &y, &z);
    fixtures::expectNear(glm::vec3(x, y, z), glm::vec3(1, 2, 3));
  }

  TEST_F(TransformRigidBodyBindingsTest, RigidBodyNonFiniteFrictionRecordsNoEditWithPositiveControl)
  {
    const auto object = fixtures::addObject(scene, "object");
    const auto body = fixtures::addRigidBody(object);
    const auto uuid = uuidOf(object);
    const auto defaultFriction = body->getFriction();

    rigidBody.setFriction(uuid.c_str(), inf);
    EXPECT_FLOAT_EQ(body->getFriction(), defaultFriction);
    EXPECT_TRUE(BindingContext::takeComponentEdits().empty());

    rigidBody.setFriction(uuid.c_str(), defaultFriction + 0.25f);
    EXPECT_FLOAT_EQ(body->getFriction(), defaultFriction + 0.25f);
    const auto edits = BindingContext::takeComponentEdits();
    ASSERT_EQ(edits.size(), 1u);
    EXPECT_EQ(edits[0].second, body);
  }

  TEST_F(TransformRigidBodyBindingsTest, RigidBodySettersAndGettersOnAMissingBodyDoNothing)
  {
    const auto bare = fixtures::addObject(scene, "bare");
    const std::string ids[] = { uuids::to_string(unknownUUID()), uuidOf(bare), "not-a-uuid" };

    for (const auto& id : ids)
    {
      rigidBody.setGravity(id.c_str(), -2.0f);
      rigidBody.setDoGravity(id.c_str(), false);
      rigidBody.setFriction(id.c_str(), 0.5f);
      rigidBody.setVelocity(id.c_str(), 1, 1, 1);
      rigidBody.setAngularVelocity(id.c_str(), 1, 1, 1);
      rigidBody.applyForce(id.c_str(), 1, 1, 1, 0, 0, 0, static_cast<int>(ForceMode::impulse));
      EXPECT_FLOAT_EQ(rigidBody.getGravity(id.c_str()), 0.0f);
      EXPECT_FLOAT_EQ(rigidBody.getFriction(id.c_str()), 0.0f);
    }
    EXPECT_TRUE(BindingContext::takeComponentEdits().empty());

    // Positive control: with a body present the same setter records an edit.
    const auto body = fixtures::addRigidBody(bare);
    rigidBody.setGravity(uuidOf(bare).c_str(), body->getGravity() - 2.0f);
    EXPECT_EQ(BindingContext::takeComponentEdits().size(), 1u);
  }

  // --- BindingContext --------------------------------------------------------------------------------

  bool stubRaycast(ObjectManager&, const glm::vec3&, const glm::vec3&, float, uint32_t, const uuids::uuid&,
                   uuids::uuid&, glm::vec3&, glm::vec3&, float&)
  {
    return false;
  }

  void stubOverlap(ObjectManager&, const glm::vec3&, float, uint32_t, const uuids::uuid&,
                   std::vector<uuids::uuid>&)
  {}

  TEST_F(TransformRigidBodyBindingsTest, BindingContextPointersRoundTrip)
  {
    EXPECT_EQ(BindingContext::getObjectManager(), scene.objectManager.get());

    BindingContext::setAssetRegistry(nullptr);
    BindingContext::setRaycast(nullptr);
    BindingContext::setOverlapSphere(nullptr);

    AssetRegistry registry;
    EXPECT_EQ(BindingContext::getAssetRegistry(), nullptr);
    BindingContext::setAssetRegistry(&registry);
    EXPECT_EQ(BindingContext::getAssetRegistry(), &registry);
    BindingContext::setAssetRegistry(nullptr);
    EXPECT_EQ(BindingContext::getAssetRegistry(), nullptr);

    EXPECT_EQ(BindingContext::getRaycast(), nullptr);
    BindingContext::setRaycast(&stubRaycast);
    EXPECT_EQ(BindingContext::getRaycast(), &stubRaycast);

    EXPECT_EQ(BindingContext::getOverlapSphere(), nullptr);
    BindingContext::setOverlapSphere(&stubOverlap);
    EXPECT_EQ(BindingContext::getOverlapSphere(), &stubOverlap);
  }

  TEST_F(TransformRigidBodyBindingsTest, BindingContextSpawnAndDestroyDrainInOrderOnce)
  {
    const auto a = fixtures::addObject(scene, "a");
    const auto b = fixtures::addObject(scene, "b");

    BindingContext::recordSpawn(a);
    BindingContext::recordSpawn(b);
    BindingContext::recordDestroy(b->getUUID());
    BindingContext::recordDestroy(a->getUUID());

    const auto spawned = BindingContext::takeSpawned();
    ASSERT_EQ(spawned.size(), 2u);
    EXPECT_EQ(spawned[0], a);
    EXPECT_EQ(spawned[1], b);

    const auto destroyed = BindingContext::takeDestroyed();
    ASSERT_EQ(destroyed.size(), 2u);
    EXPECT_EQ(destroyed[0], b->getUUID());
    EXPECT_EQ(destroyed[1], a->getUUID());

    EXPECT_TRUE(BindingContext::takeSpawned().empty());
    EXPECT_TRUE(BindingContext::takeDestroyed().empty());
  }
}
