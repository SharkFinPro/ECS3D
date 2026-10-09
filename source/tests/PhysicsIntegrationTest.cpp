#include <gtest/gtest.h>

#include "TestScene.h"
#include "CollisionSystem.h"
#include "FixedTimestep.h"
#include "PhysicsSystem.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "PhysicsTestHelpers.h"

#include <glm/ext/scalar_constants.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
  using namespace physicsFixtures;
  using fixtures::addObject;
  using fixtures::makeScene;
  using fixtures::transformOf;
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
  const auto body = addBody(object, false);

  for (int tick = 0; tick < 10; ++tick)
  {
    PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
  }

  expectNear("position", transformOf(object)->getPosition(), { 1, 2, 3 });

  // Turned back on, so the stillness above is the flag rather than an integrator that has stopped
  // running - which is what an assertion that only checks nothing happened would otherwise allow.
  body->setDoGravity(true);
  PhysicsSystem::fixedUpdate(*scene.objectManager, dt);

  EXPECT_NEAR(transformOf(object)->getPosition().y, 2.0f + gravityPerTick, 1e-5f);
}

TEST(PhysicsIntegration, ABodyInFlightKeepsItsHorizontalSpeed)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Thrown", { 0, 0, 0 });
  const auto body = addBody(object, true);

  body->setVelocity({ 1, 0, 1 });

  for (int tick = 0; tick < 10; ++tick)
  {
    PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
  }

  // Friction acts where a body touches something. It used to damp every body's horizontal velocity every
  // tick, so anything thrown drifted to a halt in mid-air.
  EXPECT_FLOAT_EQ(body->getVelocity().x, 1.0f);
  EXPECT_FLOAT_EQ(body->getVelocity().z, 1.0f);
  EXPECT_NEAR(body->getVelocity().y, 10.0f * gravityPerTick, 1e-5f);
}

TEST(PhysicsIntegration, AQueuedForceIsAppliedOnceAndThenForgotten)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Pushed", { 0, 0, 0 });
  const auto body = addBody(object, false);

  body->addPendingForce({ 0, 5, 0 }, transformOf(object)->getPosition(), ForceMode::velocityChange);

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

TEST(PhysicsIntegration, AQueuedForceChangesTheVelocityByItsMode)
{
  const auto velocityAfter = [](const ForceMode mode, const float mass)
  {
    const auto scene = makeScene();
    const auto object = addObject(scene, "Pushed", { 0, 0, 0 });
    const auto body = addBody(object, false);

    body->setMass(mass);
    body->addPendingForce({ 4, 0, 0 }, transformOf(object)->getPosition(), mode);
    PhysicsSystem::fixedUpdate(*scene.objectManager, dt);

    return body->getVelocity().x;
  };

  // Acting for the tick, a force and an acceleration are scaled by dt, the way gravity is; a force and an
  // impulse are divided by the mass.
  EXPECT_NEAR(velocityAfter(ForceMode::force, 2.0f), 4.0f * dt / 2.0f, 1e-6f);
  EXPECT_NEAR(velocityAfter(ForceMode::acceleration, 2.0f), 4.0f * dt, 1e-6f);
  EXPECT_NEAR(velocityAfter(ForceMode::impulse, 2.0f), 4.0f / 2.0f, 1e-6f);
  EXPECT_NEAR(velocityAfter(ForceMode::velocityChange, 2.0f), 4.0f, 1e-6f);

  // The same impulse moves a body of twice the mass half as fast, and so half as far; a velocity change
  // moves both alike.
  EXPECT_NEAR(velocityAfter(ForceMode::impulse, 4.0f), velocityAfter(ForceMode::impulse, 2.0f) / 2.0f, 1e-6f);
  EXPECT_NEAR(velocityAfter(ForceMode::velocityChange, 4.0f), velocityAfter(ForceMode::velocityChange, 2.0f), 1e-6f);
}

TEST(PhysicsIntegration, AForceThroughTheCentreOfMassDoesNotSpinTheBody)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Pushed", { 0, 0, 0 });
  const auto body = addBody(object, false);

  const auto transform = transformOf(object);
  PhysicsSystem::applyVelocityChange(*body, *transform, { 1, 0, 0 }, transform->getPosition(), dt);

  expectNear("velocity", body->getVelocity(), { 1, 0, 0 });
  expectNear("angular velocity", body->getAngularVelocity(), { 0, 0, 0 });
}

TEST(PhysicsIntegration, AForceAlmostThroughTheCentreIsTreatedAsThroughIt)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Pushed", { 0, 0, 0 });
  const auto body = addBody(object, false);

  const auto transform = transformOf(object);

  // Five thousandths off centre, inside the one-centimetre lever arm applyVelocityChange refuses to divide by.
  // The exactly-centred case above proves nothing about that guard - the cross product of a zero vector
  // is zero whether the guard is there or not - so this is the one that would notice it going away.
  PhysicsSystem::applyVelocityChange(*body, *transform, { 1, 0, 0 }, { 0, 0.005f, 0 }, dt);

  expectNear("velocity", body->getVelocity(), { 1, 0, 0 });
  expectNear("angular velocity", body->getAngularVelocity(), { 0, 0, 0 });
}

TEST(PhysicsIntegration, AForceOffTheCentreSpinsTheBodyThroughItsInertiaTensor)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Pushed", { 0, 0, 0 });
  const auto body = addBody(object, false);

  const auto transform = transformOf(object);

  // Pushed along +x one unit above the centre: r x F = (0,1,0) x (1,0,0) = (0,0,-1), through the inverse of
  // the inertia tensor. The push is a change of velocity, so the tensor is per unit mass. A unit-scale box
  // reaches one unit from its centre on every axis, so each diagonal is (1 + 1) / 3 and its inverse 1.5.
  PhysicsSystem::applyVelocityChange(*body, *transform, { 1, 0, 0 }, { 0, 1, 0 }, dt);

  expectNear("spin per tick", spinPerTickOf(*body), { 0, 0, -1.5f });
}

TEST(PhysicsIntegration, AVelocityChangeMovesAndTurnsABodyAlikeWhateverItsMass)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Heavy", { 0, 0, 0 });
  const auto body = addBody(object, false);

  body->setMass(20.0f);

  const auto transform = transformOf(object);
  PhysicsSystem::applyVelocityChange(*body, *transform, { 1, 0, 0 }, { 0, 1, 0 }, dt);

  // Scaling only the spin by the mass would have a contact resolve a heavy body's tipping into more fall and
  // less turn than it allows.
  expectNear("velocity", body->getVelocity(), { 1, 0, 0 });
  expectNear("spin per tick", spinPerTickOf(*body), { 0, 0, -1.5f });
}

TEST(PhysicsIntegration, AnImpulseMovesAndTurnsABodyOfTwiceTheMassHalfAsFar)
{
  const auto scene = makeScene();
  const auto light = addObject(scene, "Light", { 0, 0, 0 });
  const auto lightBody = addBody(light, false);
  const auto heavy = addObject(scene, "Heavy", { 0, 0, 0 });
  const auto heavyBody = addBody(heavy, false);

  lightBody->setMass(10.0f);
  heavyBody->setMass(20.0f);

  PhysicsSystem::applyImpulse(*lightBody, *transformOf(light), { 10, 0, 0 }, { 0, 1, 0 }, dt);
  PhysicsSystem::applyImpulse(*heavyBody, *transformOf(heavy), { 10, 0, 0 }, { 0, 1, 0 }, dt);

  // The light body gets the unit change of velocity the tests above give it directly.
  expectNear("light velocity", lightBody->getVelocity(), { 1, 0, 0 });
  expectNear("light spin per tick", spinPerTickOf(*lightBody), { 0, 0, -1.5f });

  expectNear("heavy velocity", heavyBody->getVelocity(), { 0.5f, 0, 0 });
  expectNear("heavy spin per tick", spinPerTickOf(*heavyBody), { 0, 0, -0.75f });
}

TEST(PhysicsIntegration, AWiderBodyIsHarderToSpinAboutItsShortAxis)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Wide", { 0, 0, 0 });
  const auto body = addBody(object, false);

  const auto transform = transformOf(object);
  transform->setScale({ 3, 1, 1 });
  PhysicsSystem::applyVelocityChange(*body, *transform, { 1, 0, 0 }, { 0, 1, 0 }, dt);

  // Izz takes width and height: (9 + 1) / 3, so the same push spins it at 0.3 rather than 1.5. The tensor
  // has to see the object's scale.
  expectNear("spin per tick", spinPerTickOf(*body), { 0, 0, -0.3f });
}

TEST(PhysicsIntegration, ATurnedBodySpinsThroughTheInertiaOfTheAxisNowLyingAlongTheTorque)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Turned", { 0, 0, 0 });
  const auto body = addBody(object, false);

  const auto transform = transformOf(object);
  transform->setScale({ 3, 1, 1 });
  transform->setRotation({ 0, 90, 0 });

  // The same push as above, but the long axis is turned from world x onto world z, the axis the torque
  // (0,0,-1) is about. The body now turns about one of its short axes, whose inertia is (1 + 1) / 3, so it
  // spins at 1.5 - not the 0.3 of its long axis, which is what applying the body-frame tensor to a
  // world-space torque gives.
  PhysicsSystem::applyVelocityChange(*body, *transform, { 1, 0, 0 }, { 0, 1, 0 }, dt);

  expectNear("spin per tick", spinPerTickOf(*body), { 0, 0, -1.5f });
}

TEST(PhysicsIntegration, ADegenerateScaleKeepsAngularVelocityFiniteInsteadOfSpinningToNaN)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Flattened", { 0, 0, 0 }, { 0, 0, 0 });
  const auto body = addBody(object, false);

  const auto transform = transformOf(object);

  // Every diagonal of the inertia tensor collapses to zero at this scale, so inverting it (the pre-fix
  // behaviour) produces inf, and inf times the zero cross product below is NaN. The off-centre push below
  // must be skipped rather than turned into a spin that never recovers.
  PhysicsSystem::applyVelocityChange(*body, *transform, { 1, 0, 0 }, { 0, 1, 0 }, dt);

  const auto angularVelocity = body->getAngularVelocity();
  EXPECT_TRUE(std::isfinite(angularVelocity.x));
  EXPECT_TRUE(std::isfinite(angularVelocity.y));
  EXPECT_TRUE(std::isfinite(angularVelocity.z));

  // Positive control: a normal body at the same lever arm does pick up spin, so the assertion above is
  // catching the degenerate tensor rather than a guard that swallows every off-centre push.
  const auto normalObject = addObject(scene, "Normal", { 0, 0, 0 });
  const auto normalBody = addBody(normalObject, false);
  PhysicsSystem::applyVelocityChange(*normalBody, *transformOf(normalObject), { 1, 0, 0 }, { 0, 1, 0 }, dt);

  expectNear("spin per tick", spinPerTickOf(*normalBody), { 0, 0, -1.5f });
}

TEST(PhysicsIntegration, SetMassRefusesToLeaveTheBodyAtZeroOrNegativeMass)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "MassSetting", { 0, 0, 0 });
  const auto body = addBody(object, false);

  body->setMass(0.0f);
  EXPECT_TRUE(std::isfinite(body->getMass()));
  EXPECT_GT(body->getMass(), 0.0f);

  body->setMass(-5.0f);
  EXPECT_TRUE(std::isfinite(body->getMass()));
  EXPECT_GT(body->getMass(), 0.0f);

  // Positive control: an ordinary positive mass is kept as given, not silently floored too.
  body->setMass(20.0f);
  EXPECT_FLOAT_EQ(body->getMass(), 20.0f);
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
  PhysicsSystem::handleCollision(*body, ownColliderOf(*body), ground, { 0, 0.25f, 0 }, { 0, -1, 0 }, dt);

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

TEST(PhysicsIntegration, AStaticContactDoesNotHoldBackABodyAlreadyLeavingIt)
{
  const auto scene = makeScene();

  const auto rising = addObject(scene, "Rising", { 0, 0, 0 });
  const auto body = addBody(rising, false);
  const auto ground = addObject(scene, "Ground", { 0, -2, 0 });

  body->setVelocity({ 0, 1, 0 });

  PhysicsSystem::handleCollision(*body, ownColliderOf(*body), ground, { 0, 0.25f, 0 }, { 0, -1, 0 }, dt);

  // Still moved clear of the overlap, but its velocity is kept: it is already leaving, and stopping it would
  // pull a body tipping up off an edge back down onto it.
  EXPECT_NEAR(transformOf(rising)->getPosition().y, 0.25f, 1e-5f);
  EXPECT_NEAR(body->getVelocity().y, 1.0f, 1e-5f);
}

TEST(PhysicsIntegration, AContactOffTheCenterStopsThePointItTouchesRatherThanTheWholeBody)
{
  const auto scene = makeScene();

  const auto box = addObject(scene, "Box", { 0, 0, 0 });
  const auto body = addBody(box, false);
  const auto ground = addObject(scene, "Ground", { 0, -2, 0 });

  // The spin leaves the edge sliding along the ground, which friction would then act on too.
  body->setFriction(0.0f);
  body->setVelocity({ 0, -1, 0 });

  // Under an edge, one unit to the side of the centre: r = (1, -1.01, 0) once the correction lifts the body.
  const glm::vec3 edge{ 1, -1, 0 };
  PhysicsSystem::handleCollision(*body, ownColliderOf(*body), ground, { 0, 0.01f, 0 }, edge, dt);

  // |r x n| is 1 and the inverse inertia 1.5, so the push that stops the point is 1 / (1 + 1.5) = 0.4 of the
  // fall, and it spins the body at 1.5 * 0.4 radians per tick. Stopping the whole fall and adding that spin
  // on top, as before, gave the body more energy than it landed with.
  EXPECT_NEAR(body->getVelocity().y, -0.6f, 1e-5f);
  expectNear("spin per tick", spinPerTickOf(*body), { 0, 0, 0.6f });

  const auto arm = edge - transformOf(box)->getPosition();
  EXPECT_NEAR(body->getVelocity().y + glm::cross(spinPerTickOf(*body), arm).y, 0.0f, 1e-5f);
}

TEST(PhysicsIntegration, AStaticContactStopsABodyAlikeWhateverItsMass)
{
  // Static geometry cannot move, so the push that stops the contact grows with the mass and the body's
  // response comes out the same. Off the center, so the share the spin takes is in it too.
  const auto responseOf = [](const float mass)
  {
    const auto scene = makeScene();
    const auto box = addObject(scene, "Box", { 0, 0, 0 });
    const auto body = addBody(box, false);
    const auto ground = addObject(scene, "Ground", { 0, -2, 0 });

    body->setMass(mass);
    body->setFriction(0.0f);
    body->setVelocity({ 0, -1, 0 });
    PhysicsSystem::handleCollision(*body, ownColliderOf(*body), ground, { 0, 0.01f, 0 }, { 1, -1, 0 }, dt);

    return std::pair{ body->getVelocity(), spinPerTickOf(*body) };
  };

  const auto [lightVelocity, lightSpin] = responseOf(1.0f);
  const auto [heavyVelocity, heavySpin] = responseOf(100.0f);

  expectNear("velocity", heavyVelocity, lightVelocity);
  expectNear("spin per tick", heavySpin, lightSpin);

  // The contact did act: the same numbers as the unit-mass case above.
  EXPECT_NEAR(lightVelocity.y, -0.6f, 1e-5f);
}
