#include <gtest/gtest.h>

#include "TestPrinters.h"
#include "CollisionSystem.h"
#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "PhysicsSystem.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"

#include <glm/vec3.hpp>
#include <algorithm>
#include <limits>
#include <memory>
#include <string>

namespace {
  // The tick length the engine runs at. Every number below is derived from it rather than measured, so
  // a change to the timestep shows up as a failure that names the arithmetic rather than a mystery.
  constexpr float dt = 0.1f;

  // What integrate() adds to a body's velocity each tick under the default gravity. Note that integrate
  // moves by the velocity directly rather than by velocity * dt, so "velocity" is displacement per tick -
  // while the rotation it applies below it does use dt.
  constexpr float gravityPerTick = -9.81f * dt * 0.1f;

  // The numbers below are also derived from RigidBody's defaults, not just from dt. Pinned here so that
  // changing one produces a failure that names the default rather than a page of unexplained arithmetic.
  constexpr float defaultGravity = -9.81f;
  constexpr float defaultMass = 10.0f;

  struct Scene {
    std::shared_ptr<ComponentRegistry> componentRegistry = std::make_shared<ComponentRegistry>();
    std::unique_ptr<ObjectManager> objectManager;
  };

  Scene makeScene()
  {
    Scene scene;
    registerDataComponents(*scene.componentRegistry);
    scene.objectManager = std::make_unique<ObjectManager>(scene.componentRegistry);

    return scene;
  }

  std::shared_ptr<Object> addObject(const Scene& scene, const std::string& name, const glm::vec3& position)
  {
    auto object = std::make_shared<Object>(name);
    scene.objectManager->addObject(object);
    object->getComponent<Transform>(ComponentType::transform)->setPosition(position);

    return object;
  }

  std::shared_ptr<RigidBody> addBody(const std::shared_ptr<Object>& object, const bool gravity)
  {
    auto body = std::make_shared<RigidBody>();
    object->addComponent(body);
    body->setDoGravity(gravity);

    return body;
  }

  std::shared_ptr<Transform> transformOf(const std::shared_ptr<Object>& object)
  {
    return object->getComponent<Transform>(ComponentType::transform);
  }

  void expectNear(const char* what, const glm::vec3& actual, const glm::vec3& expected)
  {
    constexpr float tolerance = 1e-4f;

    // Both vectors in the trace, not just the component that failed: a physics failure is much easier
    // to read as "expected (0.9, 1, 0.9), got (0.9, 0.9, 0.9)" than as one number out of context.
    SCOPED_TRACE(::testing::Message() << what << ": expected " << ::testing::PrintToString(expected)
                                      << ", actual " << ::testing::PrintToString(actual));

    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
  }
}

TEST(PhysicsIntegration, GravityAccumulatesInVelocityAndAddsUpInDistance)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Falling", { 0, 0, 0 });
  const auto body = addBody(object, true);

  ASSERT_FLOAT_EQ(body->getGravity(), defaultGravity);
  ASSERT_FLOAT_EQ(body->getMass(), defaultMass);

  for (int tick = 0; tick < 3; ++tick)
  {
    PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
  }

  // Velocity gains the same amount every tick, and the position gains the running total - so after three
  // ticks the body has moved 1+2+3 tick-steps rather than 3. Getting this wrong is the classic way a
  // physics change looks plausible and is not.
  EXPECT_NEAR(body->getVelocity().y, 3.0f * gravityPerTick, 1e-5f);
  EXPECT_NEAR(transformOf(object)->getPosition().y, 6.0f * gravityPerTick, 1e-5f);
}

TEST(PhysicsIntegration, ABodyWithGravityOffDoesNotMoveOnItsOwn)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Static", { 1, 2, 3 });
  addBody(object, false);

  for (int tick = 0; tick < 10; ++tick)
  {
    PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
  }

  expectNear("position", transformOf(object)->getPosition(), { 1, 2, 3 });
}

TEST(PhysicsIntegration, FrictionBleedsHorizontalVelocityAndLeavesTheVerticalAlone)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Sliding", { 0, 0, 0 });
  const auto body = addBody(object, false);

  body->setFriction(0.1f);
  body->setVelocity({ 1, 1, 1 });

  PhysicsSystem::fixedUpdate(*scene.objectManager, dt);

  // Friction is applied to the horizontal plane only, so a body sliding and falling loses the slide and
  // keeps the fall. Taking the vertical with it would make everything drift to a halt in mid-air.
  expectNear("velocity", body->getVelocity(), { 0.9f, 1.0f, 0.9f });
  expectNear("position", transformOf(object)->getPosition(), { 0.9f, 1.0f, 0.9f });
}

TEST(PhysicsIntegration, AQueuedForceIsAppliedOnceAndThenForgotten)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Pushed", { 0, 0, 0 });
  const auto body = addBody(object, false);

  body->addPendingForce({ 0, 5, 0 }, transformOf(object)->getPosition());

  PhysicsSystem::fixedUpdate(*scene.objectManager, dt);

  EXPECT_NEAR(body->getVelocity().y, 5.0f, 1e-5f);
  EXPECT_NEAR(transformOf(object)->getPosition().y, 5.0f, 1e-5f);

  // A script queues a force for the tick, not forever. Draining is what stops one jump input from
  // accelerating the body every tick after it.
  EXPECT_TRUE(body->getPendingForces().empty());

  PhysicsSystem::fixedUpdate(*scene.objectManager, dt);

  EXPECT_NEAR(body->getVelocity().y, 5.0f, 1e-5f);
  EXPECT_NEAR(transformOf(object)->getPosition().y, 10.0f, 1e-5f);
}

TEST(PhysicsIntegration, AForceThroughTheCentreOfMassDoesNotSpinTheBody)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Pushed", { 0, 0, 0 });
  const auto body = addBody(object, false);

  const auto transform = transformOf(object);
  PhysicsSystem::applyForce(*body, *transform, { 1, 0, 0 }, transform->getPosition());

  expectNear("velocity", body->getVelocity(), { 1, 0, 0 });
  expectNear("angular velocity", body->getAngularVelocity(), { 0, 0, 0 });
}

TEST(PhysicsIntegration, AForceAlmostThroughTheCentreIsTreatedAsThroughIt)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Pushed", { 0, 0, 0 });
  const auto body = addBody(object, false);

  const auto transform = transformOf(object);

  // Five thousandths off centre, inside the one-centimetre lever arm applyForce refuses to divide by.
  // The exactly-centred case above proves nothing about that guard - the cross product of a zero vector
  // is zero whether the guard is there or not - so this is the one that would notice it going away.
  PhysicsSystem::applyForce(*body, *transform, { 1, 0, 0 }, { 0, 0.005f, 0 });

  expectNear("velocity", body->getVelocity(), { 1, 0, 0 });
  expectNear("angular velocity", body->getAngularVelocity(), { 0, 0, 0 });
}

TEST(PhysicsIntegration, AForceOffTheCentreSpinsTheBodyThroughItsInertiaTensor)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Pushed", { 0, 0, 0 });
  const auto body = addBody(object, false);

  const auto transform = transformOf(object);

  // Pushed along +x one unit above the centre. The impulse is r x F = (0,1,0) x (1,0,0) = (0,0,-1),
  // divided by the inertia tensor. At the default mass of 10 and unit scale the tensor is
  // (1/12) * 10 * 0.1 * (1 + 1) = 1/6 on every diagonal, so its inverse is 6.
  PhysicsSystem::applyForce(*body, *transform, { 1, 0, 0 }, { 0, 1, 0 });

  expectNear("angular velocity", body->getAngularVelocity(), { 0, 0, -6 });
}

TEST(PhysicsIntegration, AHeavierBodyIsHarderToSpin)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Heavy", { 0, 0, 0 });
  const auto body = addBody(object, false);

  body->setMass(20.0f);

  const auto transform = transformOf(object);
  PhysicsSystem::applyForce(*body, *transform, { 1, 0, 0 }, { 0, 1, 0 });

  // Twice the mass, twice the inertia, half the spin from the same impulse. Mass entering the tensor is
  // the part a refactor is most likely to drop, and nothing else in the engine would notice.
  expectNear("angular velocity", body->getAngularVelocity(), { 0, 0, -3 });
}

TEST(PhysicsIntegration, AWiderBodyIsHarderToSpinAboutItsShortAxis)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Wide", { 0, 0, 0 });
  const auto body = addBody(object, false);

  const auto transform = transformOf(object);
  transform->setScale({ 3, 1, 1 });
  PhysicsSystem::applyForce(*body, *transform, { 1, 0, 0 }, { 0, 1, 0 });

  // Izz takes width and height: (1/12) * 10 * 0.1 * (9 + 1) = 5/6, so the same impulse spins it at 1.2
  // rather than 6. The tensor has to see the object's scale, not just its mass.
  expectNear("angular velocity", body->getAngularVelocity(), { 0, 0, -1.2f });
}

TEST(PhysicsIntegration, ACollisionAlongTheTranslationVectorCancelsTheVelocityIntoIt)
{
  const auto scene = makeScene();

  const auto falling = addObject(scene, "Falling", { 0, 0, 0 });
  const auto body = addBody(falling, false);
  // Placed anywhere: handleCollision is given the translation vector directly and never reads the other
  // object's transform when it has no rigid body of its own.
  const auto ground = addObject(scene, "Ground", { 0, -2, 0 });

  body->setVelocity({ 0, -1, 0 });

  // Pushed a quarter unit back up out of the ground, with the contact directly under the body.
  PhysicsSystem::handleCollision(*body, ground, { 0, 0.25f, 0 }, { 0, -1, 0 });

  // The correction moves the body clear, and the impulse removes exactly the velocity that was driving
  // it into the surface - so it rests rather than accumulating downward speed against something solid.
  EXPECT_NEAR(transformOf(falling)->getPosition().y, 0.25f, 1e-5f);
  EXPECT_NEAR(body->getVelocity().y, 0.0f, 1e-5f);

  // The impulse is applied at the contact point, which by then is 1.25 below the corrected centre - far
  // enough past the lever-arm guard to reach the cross product. It produces no spin only because the arm
  // and the impulse are parallel, so a swapped argument order or a sign slip in there would show up here
  // and nowhere else.
  expectNear("angular velocity", body->getAngularVelocity(), { 0, 0, 0 });
}

TEST(PhysicsIntegration, ABodyFallingOntoAStaticBoxComesToRestOnTopOfIt)
{
  const auto scene = makeScene();

  const auto ground = addObject(scene, "Ground", { 0, 0, 0 });
  ground->addComponent(std::make_shared<BoxCollider>());

  const auto falling = addObject(scene, "Falling", { 0, 5, 0 });
  falling->addComponent(std::make_shared<BoxCollider>());
  const auto body = addBody(falling, true);

  CollisionSystem collisionSystem;

  float lowest = std::numeric_limits<float>::max();
  float highest = std::numeric_limits<float>::lowest();

  // Sixty ticks: enough to land at tick eight and settle, and short enough to stay clear of the point
  // where Transform's uint8_t update counter wraps and a collider's cached bounding box goes stale.
  for (int tick = 0; tick < 60; ++tick)
  {
    PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
    collisionSystem.fixedUpdate(*scene.objectManager);

    // Sampled only once it has had every chance to settle, so the fall itself is not measured.
    if (tick >= 50)
    {
      const float height = transformOf(falling)->getPosition().y;
      lowest = std::min(lowest, height);
      highest = std::max(highest, height);
    }
  }

  // Two unit boxes, so resting on top means their centres are exactly two apart - and because the height
  // is read after the collision pass rather than between it and the integrate, the sink of one gravity
  // step is already corrected by the time it is sampled. There is nothing loose about it to allow for.
  // The hundredth is for EPA's own precision, not for slack in the result: the correction is measured
  // fresh each tick rather than accumulated, so the error does not build up over the sample window.
  EXPECT_NEAR(lowest, 2.0f, 1e-2f);
  EXPECT_NEAR(highest, 2.0f, 1e-2f);

  // The two ways this actually goes wrong, neither of which a height alone would catch: the response is
  // applied at the contact point, so a contact reported at a corner rather than under the centre would
  // spin the box, and a spinning box slides off a ground box only two units wide.
  expectNear("angular velocity", body->getAngularVelocity(), { 0, 0, 0 });

  const auto resting = transformOf(falling)->getPosition();
  EXPECT_NEAR(resting.x, 0.0f, 1e-3f);
  EXPECT_NEAR(resting.z, 0.0f, 1e-3f);
}

TEST(PhysicsIntegration, ABodyIsIntegratedOnceEvenWhenItsChildInheritsIt)
{
  const auto scene = makeScene();

  const auto parent = addObject(scene, "Parent", { 0, 0, 0 });
  const auto body = addBody(parent, true);

  auto child = std::make_shared<Object>("Child");
  child->setParent(parent);
  scene.objectManager->addObject(child);

  PhysicsSystem::fixedUpdate(*scene.objectManager, dt);

  // getComponent walks to the parent for a rigid body, so a child would otherwise be integrated with its
  // parent's body and double every force on it - once per descendant.
  EXPECT_NEAR(body->getVelocity().y, gravityPerTick, 1e-5f);
  EXPECT_NEAR(transformOf(parent)->getPosition().y, gravityPerTick, 1e-5f);
}

TEST(PhysicsIntegration, ARotationTakesTheTimestepWhereAMoveDoesNot)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Spinning", { 0, 0, 0 });
  const auto body = addBody(object, false);

  body->setFriction(0.0f);
  body->setVelocity({ 1, 0, 0 });
  body->setAngularVelocity({ 0, 10, 0 });

  PhysicsSystem::fixedUpdate(*scene.objectManager, dt);

  // The asymmetry the top of this file records, asserted rather than described: the position moves by
  // the whole velocity while the rotation moves by the angular velocity times dt. Anyone who "fixed"
  // one of the two to match the other would break every tuned value in the project.
  expectNear("position", transformOf(object)->getPosition(), { 1, 0, 0 });
  expectNear("rotation", transformOf(object)->getRotation(), { 0, 10.0f * dt, 0 });

  // And the spin is damped a percent per tick afterwards, so a body left alone stops turning.
  expectNear("angular velocity", body->getAngularVelocity(), { 0, 9.9f, 0 });
}

TEST(PhysicsIntegration, TwoBodiesClosingOnEachOtherAreSeparatedAndSlowed)
{
  const auto scene = makeScene();

  const auto left = addObject(scene, "Left", { 0, 0, 0 });
  const auto leftBody = addBody(left, false);
  const auto right = addObject(scene, "Right", { 0, 0, 0 });
  const auto rightBody = addBody(right, false);

  leftBody->setVelocity({ -1, 0, 0 });
  rightBody->setVelocity({ 1, 0, 0 });

  // Both bodies are corrected, in opposite directions - unlike the static case, where only the one with
  // a body moves. The contact is placed along the normal so the impulse produces no torque of its own.
  PhysicsSystem::handleCollision(*leftBody, right, { 1, 0, 0 }, { 2, 0, 0 });

  expectNear("left position", transformOf(left)->getPosition(), { 1, 0, 0 });
  expectNear("right position", transformOf(right)->getPosition(), { -1, 0, 0 });

  // They are closing, so the impulse fires: the relative velocity along the normal is 2, and it goes to
  // the body this call is for. The other gets its own call from its own edge.
  expectNear("left velocity", leftBody->getVelocity(), { 1, 0, 0 });
  expectNear("right velocity", rightBody->getVelocity(), { 1, 0, 0 });
  expectNear("left spin", leftBody->getAngularVelocity(), { 0, 0, 0 });
}

TEST(PhysicsIntegration, TwoBodiesAlreadyMovingApartAreSeparatedButNotSlowed)
{
  const auto scene = makeScene();

  const auto left = addObject(scene, "Left", { 0, 0, 0 });
  const auto leftBody = addBody(left, false);
  const auto right = addObject(scene, "Right", { 0, 0, 0 });
  addBody(right, false);

  leftBody->setVelocity({ 1, 0, 0 });
  right->getComponent<RigidBody>(ComponentType::rigidBody)->setVelocity({ -1, 0, 0 });

  PhysicsSystem::handleCollision(*leftBody, right, { 1, 0, 0 }, { 2, 0, 0 });

  // Still pushed apart - an overlap is an overlap - but no impulse, because they are already separating
  // and adding one would fling apart two bodies that were resolving themselves.
  expectNear("left position", transformOf(left)->getPosition(), { 1, 0, 0 });
  expectNear("left velocity", leftBody->getVelocity(), { 1, 0, 0 });
}
