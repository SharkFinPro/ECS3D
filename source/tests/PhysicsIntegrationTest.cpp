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
#include <limits>
#include <memory>
#include <string>
#include <utility>

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

  using fixtures::addObject;
  using fixtures::makeScene;
  using fixtures::transformOf;

  std::shared_ptr<RigidBody> addBody(const std::shared_ptr<Object>& object, const bool gravity)
  {
    auto body = fixtures::addRigidBody(object);
    body->setDoGravity(gravity);

    return body;
  }

  // Looser than the fixture default: these values come out of an accumulation over several ticks, not
  // out of a single operation.
  void expectNear(const char* what, const glm::vec3& actual, const glm::vec3& expected)
  {
    fixtures::expectNear(what, actual, expected, 1e-4f);
  }

  // A body's spin in radians per tick, the units a push is in, rather than the degrees per second it is
  // stored in.
  glm::vec3 spinPerTickOf(const RigidBody& body)
  {
    return glm::radians(body.getAngularVelocity()) * dt;
  }

  // Where a body's local up axis points, composed the way BoxCollider places its vertices, so the tests
  // read a rotation the way collisions do rather than through the code under test.
  glm::vec3 localUpOf(const glm::vec3& rotationDegrees)
  {
    const auto orientation = glm::rotate(glm::mat4(1.0f), glm::radians(rotationDegrees.z), { 0, 0, 1 })
      * glm::rotate(glm::mat4(1.0f), glm::radians(rotationDegrees.y), { 0, 1, 0 })
      * glm::rotate(glm::mat4(1.0f), glm::radians(rotationDegrees.x), { 1, 0, 0 });

    return glm::vec3(orientation * glm::vec4(0, 1, 0, 0));
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
  PhysicsSystem::handleCollision(*body, ground, { 0, 0.25f, 0 }, { 0, -1, 0 }, dt);

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

  PhysicsSystem::handleCollision(*body, ground, { 0, 0.25f, 0 }, { 0, -1, 0 }, dt);

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
  PhysicsSystem::handleCollision(*body, ground, { 0, 0.01f, 0 }, edge, dt);

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
    PhysicsSystem::handleCollision(*body, ground, { 0, 0.01f, 0 }, { 1, -1, 0 }, dt);

    return std::pair{ body->getVelocity(), spinPerTickOf(*body) };
  };

  const auto [lightVelocity, lightSpin] = responseOf(1.0f);
  const auto [heavyVelocity, heavySpin] = responseOf(100.0f);

  expectNear("velocity", heavyVelocity, lightVelocity);
  expectNear("spin per tick", heavySpin, lightSpin);

  // The contact did act: the same numbers as the unit-mass case above.
  EXPECT_NEAR(lightVelocity.y, -0.6f, 1e-5f);
}

TEST(PhysicsIntegration, ABodyFallingOntoAStaticBoxComesToRestOnTopOfIt)
{
  const auto scene = makeScene();

  const auto ground = addObject(scene, "Ground", { 0, 0, 0 });
  ground->addComponent(std::make_shared<BoxCollider>());

  const auto falling = addObject(scene, "Falling", { 0, 5, 0 });
  falling->addComponent(std::make_shared<BoxCollider>());
  addBody(falling, true);

  CollisionSystem collisionSystem;

  float lowest = std::numeric_limits<float>::max();
  float highest = std::numeric_limits<float>::lowest();

  // Sixty ticks: enough to land at tick eight and settle, and short enough to stay clear of the point
  // where Transform's uint8_t update counter wraps and a collider's cached bounding box goes stale.
  for (int tick = 0; tick < 60; ++tick)
  {
    PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
    collisionSystem.fixedUpdate(*scene.objectManager, dt);

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
  EXPECT_GT(lowest, 1.5f);
  EXPECT_LT(highest, 3.0f);
}

TEST(PhysicsIntegration, ABoxLandingFlatOnAStaticBoxSettlesWithoutSpinning)
{
  const auto scene = makeScene();

  const auto ground = addObject(scene, "Ground", { 0, 0, 0 });
  ground->addComponent(std::make_shared<BoxCollider>());

  const auto falling = addObject(scene, "Falling", { 0, 5, 0 });
  falling->addComponent(std::make_shared<BoxCollider>());
  const auto body = addBody(falling, true);

  CollisionSystem collisionSystem;

  // Two axis-aligned unit boxes dropped straight down onto each other touch across their whole
  // footprint at once. findCollisionPoint used to report that contact at a corner of the touching
  // face rather than its centre, giving the impulse a lever arm it should not have had and leaving the
  // body rocking at roughly 0.8 angular velocity on two axes once it "settled". With the contact placed
  // at the manifold's centroid there is no lever arm at all, so the body should come to rest with
  // negligible spin.
  float maxAngularSpeed = 0.0f;

  for (int tick = 0; tick < 60; ++tick)
  {
    PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
    collisionSystem.fixedUpdate(*scene.objectManager, dt);

    if (tick >= 50)
    {
      maxAngularSpeed = std::max(maxAngularSpeed, glm::length(body->getAngularVelocity()));
    }
  }

  EXPECT_LT(maxAngularSpeed, 0.05f);
}

TEST(PhysicsIntegration, ABoxLandingOnACornerStillPicksUpSpin)
{
  const auto scene = makeScene();

  const auto ground = addObject(scene, "Ground", { 0, 0, 0 });
  ground->addComponent(std::make_shared<BoxCollider>());

  // Tilted about one axis before it falls, so it comes down onto a single edge/corner of the ground box
  // rather than landing flat - a genuine off-centre contact. This is the positive control alongside the
  // test above: the fix that centres a flat-face contact must not also flatten this one to zero, since a
  // real off-centre impulse should still produce torque.
  const auto falling = addObject(scene, "Falling", { 0, 5, 0 });
  falling->addComponent(std::make_shared<BoxCollider>());
  const auto body = addBody(falling, true);
  transformOf(falling)->setRotation({ 0, 0, 25 });

  CollisionSystem collisionSystem;

  float maxAngularSpeed = 0.0f;

  for (int tick = 0; tick < 20; ++tick)
  {
    PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
    collisionSystem.fixedUpdate(*scene.objectManager, dt);

    maxAngularSpeed = std::max(maxAngularSpeed, glm::length(body->getAngularVelocity()));
  }

  EXPECT_GT(maxAngularSpeed, 0.1f);
}

TEST(PhysicsIntegration, ABoxLandingOnAnEdgeFallsOntoItsFaceRatherThanCreepingOver)
{
  const auto scene = makeScene();

  const auto ground = addObject(scene, "Ground", { 0, 0, 0 });
  fixtures::addBoxCollider(ground);

  // Tilted 25 degrees, it lands on one edge with its center of mass over the ground, so the support at that
  // edge turns it down onto its face. The spin a support gives used to be thousands of times too weak, and
  // after these thirty ticks the box was still leaning more than ten degrees.
  const auto falling = addObject(scene, "Falling", { 0, 5, 0 });
  fixtures::addBoxCollider(falling);
  addBody(falling, true);
  transformOf(falling)->setRotation({ 0, 0, 25 });

  CollisionSystem collisionSystem;

  for (int tick = 0; tick < 30; ++tick)
  {
    PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
    collisionSystem.fixedUpdate(*scene.objectManager, dt);
  }

  fixtures::expectNear("up", localUpOf(transformOf(falling)->getRotation()), { 0, 1, 0 }, 1e-3f);
  EXPECT_NEAR(transformOf(falling)->getPosition().y, 2.0f, 0.1f);
}

TEST(PhysicsIntegration, ABoxWhoseCenterOverhangsALedgeTipsOffIt)
{
  const auto scene = makeScene();

  // The ground ends at x = 3, and the box's center sits 0.3 past it: the support is only the strip of its
  // underside still over the ground, all on one side of the center.
  const auto ground = addObject(scene, "Ground", { 0, 0, 0 }, { 3, 1, 3 });
  fixtures::addBoxCollider(ground);

  const auto box = addObject(scene, "Box", { 3.3f, 2, 0 });
  fixtures::addBoxCollider(box);
  addBody(box, true);

  CollisionSystem collisionSystem;

  for (int tick = 0; tick < 10; ++tick)
  {
    PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
    collisionSystem.fixedUpdate(*scene.objectManager, dt);
  }

  // A second in, it has tipped well over the edge. It used to have turned about a degree.
  EXPECT_LT(localUpOf(transformOf(box)->getRotation()).y, std::cos(glm::radians(20.0f)));
}

namespace {
  // Where a point fixed in a body at local, relative to its center, is now, composed the way localUpOf is.
  glm::vec3 worldPointOf(const Transform& transform, const glm::vec3& local)
  {
    const auto rotation = transform.getRotation();
    const auto orientation = glm::rotate(glm::mat4(1.0f), glm::radians(rotation.z), { 0, 0, 1 })
      * glm::rotate(glm::mat4(1.0f), glm::radians(rotation.y), { 0, 1, 0 })
      * glm::rotate(glm::mat4(1.0f), glm::radians(rotation.x), { 1, 0, 0 });

    return transform.getPosition() + glm::vec3(orientation * glm::vec4(local, 0));
  }

  // The same box as above, tipping over the ledge, after six ticks. Returns where the point of its underside
  // that started on the ledge's edge is then, and how far the box has turned.
  std::pair<glm::vec3, float> ledgePivotAfterTipping(const float friction)
  {
    const auto scene = makeScene();

    const auto ground = addObject(scene, "Ground", { 0, 0, 0 }, { 3, 1, 3 });
    fixtures::addBoxCollider(ground);

    const auto box = addObject(scene, "Box", { 3.3f, 2, 0 });
    fixtures::addBoxCollider(box);
    addBody(box, true)->setFriction(friction);

    CollisionSystem collisionSystem;

    for (int tick = 0; tick < 6; ++tick)
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
      collisionSystem.fixedUpdate(*scene.objectManager, dt);
    }

    const auto& transform = *transformOf(box);
    const float tilt = glm::degrees(std::acos(std::clamp(localUpOf(transform.getRotation()).y, -1.0f, 1.0f)));

    return { worldPointOf(transform, { -0.3f, -1, 0 }), tilt };
  }
}

TEST(PhysicsIntegration, ABoxTippingOffALedgeTurnsAboutTheEdgeRatherThanSlidingOverIt)
{
  // Friction at the edge holds the underside there while the box turns about it.
  const auto [pivot, tilt] = ledgePivotAfterTipping(0.5f);

  EXPECT_GT(tilt, 10.0f);
  fixtures::expectNear("pivot", pivot, { 3, 1, 0 }, 0.02f);

  // Positive control: without friction the same point slides back along the ledge as the box turns.
  const auto [slidingPivot, slidingTilt] = ledgePivotAfterTipping(0.0f);

  EXPECT_GT(slidingTilt, 10.0f);
  EXPECT_LT(slidingPivot.x, 2.9f);
}

namespace {
  // A unit box dropped a unit onto a wide static ground while moving sideways at three units per second.
  struct Landed {
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec3 up;
  };

  Landed landSliding(const float friction)
  {
    const auto scene = makeScene();

    const auto ground = addObject(scene, "Ground", { 0, 0, 0 }, { 50, 1, 50 });
    fixtures::addBoxCollider(ground);

    const auto box = addObject(scene, "Box", { 0, 3, 0 });
    fixtures::addBoxCollider(box);
    const auto body = addBody(box, true);
    body->setFriction(friction);
    body->setVelocity({ 3.0f * dt, 0, 0 });

    CollisionSystem collisionSystem;

    for (int tick = 0; tick < 30; ++tick)
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
      collisionSystem.fixedUpdate(*scene.objectManager, dt);
    }

    return { transformOf(box)->getPosition(), body->getVelocity(), localUpOf(transformOf(box)->getRotation()) };
  }
}

TEST(PhysicsIntegration, ABoxLandingWithSidewaysSpeedStopsSlidingWithinAShortDistance)
{
  // The landing's own impulse takes a share of the slide, and each resting tick after it takes friction times
  // the weight that tick: the box stops about a unit and a half from where it came down, lying flat.
  const auto landed = landSliding(0.5f);

  EXPECT_LT(landed.position.x, 2.0f);
  EXPECT_LT(glm::length(glm::vec2(landed.velocity.x, landed.velocity.z)), 1e-4f);
  fixtures::expectNear("up", landed.up, { 0, 1, 0 }, 1e-3f);
  EXPECT_NEAR(landed.position.y, 2.0f, 0.05f);

  // Positive control: on a frictionless contact it keeps all thirty ticks of its slide.
  EXPECT_GT(landSliding(0.0f).position.x, 8.0f);
}

namespace {
  // A heavy box resting on a light one, which rests on a wide static ground, is set sliding. Returns where both
  // boxes are and how fast the top one still moves forty ticks later.
  struct Slid {
    glm::vec3 lower;
    glm::vec3 upper;
    float upperSpeed;
  };

  Slid slideTheTopOfAStack(const float lowerMass, const float upperMass)
  {
    const auto scene = makeScene();

    const auto ground = addObject(scene, "Ground", { 0, 0, 0 }, { 20, 1, 20 });
    fixtures::addBoxCollider(ground);

    const auto lower = addObject(scene, "Lower", { 0, 2, 0 });
    fixtures::addBoxCollider(lower);
    addBody(lower, true)->setMass(lowerMass);

    const auto upper = addObject(scene, "Upper", { 0, 4, 0 });
    fixtures::addBoxCollider(upper);
    const auto upperBody = addBody(upper, true);
    upperBody->setMass(upperMass);

    CollisionSystem collisionSystem;

    for (int tick = 0; tick < 50; ++tick)
    {
      if (tick == 10)
      {
        upperBody->setVelocity({ 2.0f * dt, 0, 0 });
      }

      PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
      collisionSystem.fixedUpdate(*scene.objectManager, dt);
    }

    return { transformOf(lower)->getPosition(), transformOf(upper)->getPosition(),
             glm::length(upperBody->getVelocity()) };
  }
}

TEST(PhysicsIntegration, ABoxSlidingOnAnotherIsHeldBackByFrictionWhileTheOneUnderItStaysPut)
{
  // The lower box's own support holds it against the drag, since friction there carries the weight of the
  // whole stack, so the upper box slows at friction times its weight whatever the masses. Sized as if the light
  // box could move freely, the heavy one would barely slow and slide off.
  for (const auto& [lowerMass, upperMass] : { std::pair{ 10.0f, 100.0f }, std::pair{ 100.0f, 10.0f } })
  {
    SCOPED_TRACE(testing::Message() << "lower mass " << lowerMass << ", upper mass " << upperMass);

    const auto slid = slideTheTopOfAStack(lowerMass, upperMass);

    fixtures::expectNear("lower", slid.lower, { 0, 2, 0 }, 0.02f);
    EXPECT_GT(slid.upper.x, 0.2f);
    EXPECT_LT(slid.upper.x, 0.8f);
    EXPECT_GT(slid.upper.y, 3.8f);
    EXPECT_LT(slid.upperSpeed, 1e-3f);
  }
}

TEST(PhysicsIntegration, ASmallerBoxRestingNearTheEdgeOfALargerOneDoesNotSpin)
{
  const auto scene = makeScene();

  // A larger, flat static box; the falling box's whole footprint stays within it, offset toward one
  // edge rather than centred, so the contact face is the falling box's own bottom face rather than a
  // clipped sliver of it.
  const auto ground = addObject(scene, "Ground", { 0, 0, 0 }, { 3, 1, 3 });
  ground->addComponent(std::make_shared<BoxCollider>());

  const auto falling = addObject(scene, "Falling", { 1.5f, 5, 0 });
  falling->addComponent(std::make_shared<BoxCollider>());
  const auto body = addBody(falling, true);

  CollisionSystem collisionSystem;

  float maxAngularSpeed = 0.0f;

  for (int tick = 0; tick < 60; ++tick)
  {
    PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
    collisionSystem.fixedUpdate(*scene.objectManager, dt);

    if (tick >= 50)
    {
      maxAngularSpeed = std::max(maxAngularSpeed, glm::length(body->getAngularVelocity()));
    }
  }

  EXPECT_LT(maxAngularSpeed, 0.05f);
}

TEST(PhysicsIntegration, ABoxOverhangingTheEdgeOfALargerBoxStaysSupportedRatherThanFallingThroughOrOff)
{
  const auto scene = makeScene();

  // The falling box's footprint (x from 1.2 to 3.2) hangs 0.2 past the ground's edge at x = 3, but its
  // centre (2.2) is still comfortably over the ground - a real box resting near a table's edge, whose
  // whole underside bears on the table rather than just one corner.
  const auto ground = addObject(scene, "Ground", { 0, 0, 0 }, { 3, 1, 3 });
  ground->addComponent(std::make_shared<BoxCollider>());

  const auto falling = addObject(scene, "Falling", { 2.2f, 5, 0 });
  falling->addComponent(std::make_shared<BoxCollider>());
  const auto body = addBody(falling, true);

  CollisionSystem collisionSystem;

  float lowest = std::numeric_limits<float>::max();
  float highest = std::numeric_limits<float>::lowest();

  // The contact manifold spans the clipped underside, so the support impulses can balance the box
  // about its centre of mass even though the overlap's centroid sits off it. It should come to rest
  // without spin, on top of the ground rather than sinking through it or rocking off the edge.
  float maxAngularSpeed = 0.0f;

  for (int tick = 0; tick < 150; ++tick)
  {
    PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
    collisionSystem.fixedUpdate(*scene.objectManager, dt);

    if (tick >= 120)
    {
      const float height = transformOf(falling)->getPosition().y;
      lowest = std::min(lowest, height);
      highest = std::max(highest, height);
      maxAngularSpeed = std::max(maxAngularSpeed, glm::length(body->getAngularVelocity()));
    }
  }

  EXPECT_GT(lowest, 1.5f);
  EXPECT_LT(highest, 3.0f);
  EXPECT_LT(maxAngularSpeed, 0.05f);
}

TEST(PhysicsIntegration, AYawedBoxLandingFlatOnAStaticBoxSettlesWithoutSpinning)
{
  const auto scene = makeScene();

  const auto ground = addObject(scene, "Ground", { 0, 0, 0 });
  ground->addComponent(std::make_shared<BoxCollider>());

  // Yawed 45 degrees about the vertical axis before it falls, straight down onto a same-size ground
  // box: its footprint is still centred, but rotated so each of its four bottom corners individually
  // pokes outside the ground's own axis-aligned footprint even though the two faces plainly overlap - a
  // vertex-inside-footprint test alone would find nothing on either side and fall through to the old,
  // single-corner answer this change replaces.
  const auto falling = addObject(scene, "Falling", { 0, 5, 0 });
  falling->addComponent(std::make_shared<BoxCollider>());
  const auto body = addBody(falling, true);
  transformOf(falling)->setRotation({ 0, 45, 0 });

  CollisionSystem collisionSystem;

  float maxAngularSpeed = 0.0f;

  for (int tick = 0; tick < 60; ++tick)
  {
    PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
    collisionSystem.fixedUpdate(*scene.objectManager, dt);

    if (tick >= 50)
    {
      maxAngularSpeed = std::max(maxAngularSpeed, glm::length(body->getAngularVelocity()));
    }
  }

  EXPECT_LT(maxAngularSpeed, 0.05f);
}

namespace {
  // A unit box resting on a static ground box, with a unit sphere resting on the box offset along x.
  // Returns the lowest the box's center sits, read after each collision pass once the stack has settled.
  float lowestSettledHeightOfABoxUnderASphere(const float sphereOffsetX)
  {
    const auto scene = makeScene();

    const auto ground = addObject(scene, "Ground", { 0, 0, 0 }, { 5, 1, 5 });
    fixtures::addBoxCollider(ground);

    const auto box = addObject(scene, "Box", { 0, 2, 0 });
    fixtures::addBoxCollider(box);
    addBody(box, true);

    const auto sphere = addObject(scene, "Sphere", { sphereOffsetX, 4, 0 });
    fixtures::addSphereCollider(sphere, 1.0f);
    addBody(sphere, true);

    CollisionSystem collisionSystem;

    float lowest = std::numeric_limits<float>::max();

    for (int tick = 0; tick < 60; ++tick)
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
      collisionSystem.fixedUpdate(*scene.objectManager, dt);

      if (tick >= 40)
      {
        lowest = std::min(lowest, transformOf(box)->getPosition().y);
      }
    }

    return lowest;
  }
}

TEST(PhysicsIntegration, ABoxUnderASphereStaysOnTheGroundWhicheverSideOfItTheSphereSits)
{
  // The box and the sphere are both two wide, so a sphere offset toward +x puts the box ahead of it in
  // the sweep's minX order and one offset toward -x puts it behind. Resolving the box first let the
  // sphere's push drive it back into the ground for the rest of every tick, about halfway through;
  // the mirrored stack stayed put.
  const float sphereTowardPositiveX = lowestSettledHeightOfABoxUnderASphere(0.5f);
  const float sphereTowardNegativeX = lowestSettledHeightOfABoxUnderASphere(-0.5f);

  // Resting on the ground puts the box's center at 2.
  EXPECT_GT(sphereTowardPositiveX, 1.9f);
  EXPECT_GT(sphereTowardNegativeX, 1.9f);
  EXPECT_NEAR(sphereTowardPositiveX, sphereTowardNegativeX, 0.01f);
}

namespace {
  // A unit box of lowerMass resting on a static ground box, with a unit box of upperMass dropped onto it.
  // Returns the extremes of both heights, read after each collision pass once the stack has settled, and the
  // fastest the upper box still moves then.
  struct SettledStack {
    float lowestLower;
    float highestLower;
    float lowestUpper;
    float highestUpper;
    float fastestUpper;
  };

  SettledStack settleAStack(const float lowerMass, const float upperMass)
  {
    const auto scene = makeScene();

    const auto ground = addObject(scene, "Ground", { 0, 0, 0 }, { 5, 1, 5 });
    fixtures::addBoxCollider(ground);

    const auto lower = addObject(scene, "Lower", { 0, 2, 0 });
    fixtures::addBoxCollider(lower);
    addBody(lower, true)->setMass(lowerMass);

    const auto upper = addObject(scene, "Upper", { 0, 4.2f, 0 });
    fixtures::addBoxCollider(upper);
    const auto upperBody = addBody(upper, true);
    upperBody->setMass(upperMass);

    CollisionSystem collisionSystem;

    SettledStack settled{ std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
                          std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(), 0.0f };

    for (int tick = 0; tick < 100; ++tick)
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
      collisionSystem.fixedUpdate(*scene.objectManager, dt);

      if (tick >= 60)
      {
        const float lowerHeight = transformOf(lower)->getPosition().y;
        const float upperHeight = transformOf(upper)->getPosition().y;
        settled.lowestLower = std::min(settled.lowestLower, lowerHeight);
        settled.highestLower = std::max(settled.highestLower, lowerHeight);
        settled.lowestUpper = std::min(settled.lowestUpper, upperHeight);
        settled.highestUpper = std::max(settled.highestUpper, upperHeight);
        settled.fastestUpper = std::max(settled.fastestUpper, glm::length(upperBody->getVelocity()));
      }
    }

    return settled;
  }
}

TEST(PhysicsIntegration, AStackHoldsWhateverTheMassesInIt)
{
  // Resolved one contact at a time, a heavy box trades so little of its fall with a light one that it would
  // gain speed every tick, driving the light box into the ground and sinking into it. Each support holds up
  // what rests on it instead. The upper box's contact is resolved before the lower box's, which puts it up to
  // one tick of gravity low, depending on whether the lower box's own pass meets it again.
  for (const auto& [lowerMass, upperMass] : { std::pair{ 10.0f, 10.0f }, std::pair{ 1.0f, 500.0f },
                                              std::pair{ 500.0f, 1.0f } })
  {
    SCOPED_TRACE(testing::Message() << "lower mass " << lowerMass << ", upper mass " << upperMass);

    const auto settled = settleAStack(lowerMass, upperMass);

    // Loose about where, since the narrow phase measures the overlap only so closely, but not about sinking.
    EXPECT_NEAR(settled.lowestLower, 2.0f, 0.02f);
    EXPECT_NEAR(settled.highestLower, 2.0f, 0.02f);
    EXPECT_GT(settled.lowestUpper, 4.0f + gravityPerTick - 0.02f);
    EXPECT_LT(settled.highestUpper, 4.02f);
    EXPECT_LT(settled.fastestUpper, 0.01f);
  }
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

TEST(PhysicsIntegration, SpinTurnsABodyAboutTheWorldAxisEvenWhenItIsAlreadyTurned)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Tilted", { 0, 0, 0 });
  const auto body = addBody(object, false);

  // Turned half way round and tilted 20 degrees, so its up axis leans toward -z. Spin about world +x
  // leans it back toward +z: ten degrees in one tick at 100 degrees per second.
  const glm::vec3 tilted{ 20, 180, 0 };
  expectNear("initial up", localUpOf(tilted), { 0, glm::cos(glm::radians(20.0f)), -glm::sin(glm::radians(20.0f)) });

  transformOf(object)->setRotation(tilted);
  body->setAngularVelocity({ 100, 0, 0 });

  PhysicsSystem::fixedUpdate(*scene.objectManager, dt);

  // Adding the spin to the x angle turns about the body's own x axis instead, which the half turn has
  // reversed - the tilt grows to 30 degrees, and a torque meant to right the body topples it.
  expectNear("up", localUpOf(transformOf(object)->getRotation()),
             { 0, glm::cos(glm::radians(10.0f)), -glm::sin(glm::radians(10.0f)) });
}

TEST(PhysicsIntegration, ASpinTooSlowToSeeIsKeptButDoesNotRewriteTheRotation)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Drifting", { 0, 0, 0 });
  const auto body = addBody(object, false);

  const glm::vec3 rotation{ 10, 20, 30 };
  transformOf(object)->setRotation(rotation);
  body->setAngularVelocity({ 0, 0.005f, 0 });

  PhysicsSystem::fixedUpdate(*scene.objectManager, dt);

  // Compared exactly: turning by a spin this small only rewrites the angles by float noise, every tick.
  EXPECT_EQ(transformOf(object)->getRotation().x, rotation.x);
  EXPECT_EQ(transformOf(object)->getRotation().y, rotation.y);
  EXPECT_EQ(transformOf(object)->getRotation().z, rotation.z);

  // Still there for a torque to build on, so a slow start to tipping is not lost - just damped as usual.
  expectNear("angular velocity", body->getAngularVelocity(), { 0, 0.005f * 0.99f, 0 });
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
  // a body moves. Equal masses split the overlap evenly. The contact is placed along the normal so the
  // impulse produces no torque of its own.
  PhysicsSystem::handleCollision(*leftBody, right, { 1, 0, 0 }, { 2, 0, 0 }, dt);

  expectNear("left position", transformOf(left)->getPosition(), { 0.5f, 0, 0 });
  expectNear("right position", transformOf(right)->getPosition(), { -0.5f, 0, 0 });

  // They are closing, so the impulse fires: the relative velocity along the normal is 2, applied to this
  // body and subtracted from the other in the same call. Both end up moving the way the one they hit was
  // going - which is what makes a collision between two dynamic bodies conserve anything.
  expectNear("left velocity", leftBody->getVelocity(), { 1, 0, 0 });
  expectNear("right velocity", rightBody->getVelocity(), { -1, 0, 0 });

  // The contact sits along the normal from both centres, so neither impulse has a lever arm.
  expectNear("left spin", leftBody->getAngularVelocity(), { 0, 0, 0 });
  expectNear("right spin", rightBody->getAngularVelocity(), { 0, 0, 0 });
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

  PhysicsSystem::handleCollision(*leftBody, right, { 1, 0, 0 }, { 2, 0, 0 }, dt);

  // Still pushed apart - an overlap is an overlap - but no impulse to either body, because they are
  // already separating and adding one would fling apart two bodies that were resolving themselves.
  expectNear("left position", transformOf(left)->getPosition(), { 0.5f, 0, 0 });
  expectNear("left velocity", leftBody->getVelocity(), { 1, 0, 0 });
  expectNear("right velocity", right->getComponent<RigidBody>(ComponentType::rigidBody)->getVelocity(),
             { -1, 0, 0 });
}

TEST(PhysicsIntegration, ALightBodyHittingAHeavyOneReboundsWhileTheHeavyOneBarelyMoves)
{
  const auto scene = makeScene();

  const auto light = addObject(scene, "Light", { 0, 0, 0 });
  const auto lightBody = addBody(light, false);
  const auto heavy = addObject(scene, "Heavy", { 2, 0, 0 });
  const auto heavyBody = addBody(heavy, false);

  lightBody->setMass(1.0f);
  heavyBody->setMass(9.0f);
  lightBody->setVelocity({ 1, 0, 0 });

  // Overlapping by a tenth, touching on the line between the centers.
  PhysicsSystem::handleCollision(*lightBody, heavy, { -0.1f, 0, 0 }, { 1, 0, 0 }, dt);

  // An elastic collision: (1 - 9) / 10 of the light body's speed comes back, and 2 / 10 of it goes on in the
  // heavy one. Momentum is kept.
  expectNear("light velocity", lightBody->getVelocity(), { -0.8f, 0, 0 });
  expectNear("heavy velocity", heavyBody->getVelocity(), { 0.2f, 0, 0 });
  EXPECT_NEAR(lightBody->getVelocity().x * 1.0f + heavyBody->getVelocity().x * 9.0f, 1.0f, 1e-5f);

  // The overlap is split by inverse mass too: nine tenths of it is the light body's to clear.
  expectNear("light position", transformOf(light)->getPosition(), { -0.09f, 0, 0 });
  expectNear("heavy position", transformOf(heavy)->getPosition(), { 2.01f, 0, 0 });
}

namespace {
  // A heavy body landing at unit speed on a light one, which is either resting on a support or in the air.
  struct Landing {
    glm::vec3 upperVelocity;
    glm::vec3 lowerVelocity;
    glm::vec3 upperPosition;
    glm::vec3 lowerPosition;
  };

  Landing landOnALightBody(const bool lowerIsResting)
  {
    const auto scene = makeScene();

    const auto lower = addObject(scene, "Lower", { 0, 0, 0 });
    const auto lowerBody = addBody(lower, false);
    const auto upper = addObject(scene, "Upper", { 0, 2, 0 });
    const auto upperBody = addBody(upper, false);

    lowerBody->setMass(1.0f);
    upperBody->setMass(100.0f);
    lowerBody->setFalling(!lowerIsResting);

    // Mid-tick, the lower body has fallen by a tick of gravity that its own support has not yet taken back.
    lowerBody->setVelocity({ 0, gravityPerTick, 0 });
    upperBody->setVelocity({ 0, -1, 0 });

    PhysicsSystem::handleCollision(*upperBody, lower, { 0, 0.1f, 0 }, { 0, 1, 0 }, dt);

    return { upperBody->getVelocity(), lowerBody->getVelocity(), transformOf(upper)->getPosition(),
             transformOf(lower)->getPosition() };
  }
}

TEST(PhysicsIntegration, ABodyLandingOnAnotherRestingOnASupportStopsAsOnStaticGeometry)
{
  // The support takes the push for the body resting on it, however much heavier what lands is, so the landing
  // body stops dead and the resting one is neither driven into its support nor moved into it.
  const auto resting = landOnALightBody(true);

  expectNear("upper velocity", resting.upperVelocity, { 0, 0, 0 });
  expectNear("lower velocity", resting.lowerVelocity, { 0, gravityPerTick, 0 });
  expectNear("upper position", resting.upperPosition, { 0, 2.1f, 0 });
  expectNear("lower position", resting.lowerPosition, { 0, 0, 0 });

  // In the air, the light body takes the push by its mass: an elastic collision barely slows the heavy one.
  const auto falling = landOnALightBody(false);

  EXPECT_LT(falling.upperVelocity.y, -0.9f);
  EXPECT_LT(falling.lowerVelocity.y, -1.5f);
}

TEST(PhysicsIntegration, ARestingBodyPushedFromTheSideMovesByItsMass)
{
  const auto scene = makeScene();

  const auto resting = addObject(scene, "Resting", { 2, 0, 0 });
  const auto restingBody = addBody(resting, false);
  const auto hitter = addObject(scene, "Hitter", { 0, 0, 0 });
  const auto hitterBody = addBody(hitter, false);

  restingBody->setMass(9.0f);
  restingBody->setFalling(false);
  hitterBody->setMass(1.0f);
  hitterBody->setVelocity({ 1, 0, 0 });

  // Its support takes nothing of a push along it, so a resting body answers a sideways hit as a free one does.
  PhysicsSystem::handleCollision(*hitterBody, resting, { -0.1f, 0, 0 }, { 1, 0, 0 }, dt);

  expectNear("hitter velocity", hitterBody->getVelocity(), { -0.8f, 0, 0 });
  expectNear("resting velocity", restingBody->getVelocity(), { 0.2f, 0, 0 });
}

TEST(PhysicsIntegration, ASpinningBoxResolvedAcrossAManifoldIsNotFlungByItsOwnSpin)
{
  const auto scene = makeScene();

  const auto box = addObject(scene, "Box", { 0, 0, 0 });
  const auto body = addBody(box, false);
  const auto ground = addObject(scene, "Ground", { 0, -1, 0 });

  // Angular velocity is in degrees per second and linear velocity in units per tick. Read as the same
  // unit, this spin has two underside corners closing on the ground at 90 units per tick, which a solve
  // over the manifold once turned into a linear kick of that order.
  body->setAngularVelocity({ 180, 0, 0 });

  const std::array<glm::vec3, 4> underside{
    glm::vec3{ -0.5f, -0.5f, -0.5f }, glm::vec3{ 0.5f, -0.5f, -0.5f },
    glm::vec3{ 0.5f, -0.5f, 0.5f }, glm::vec3{ -0.5f, -0.5f, 0.5f }
  };

  PhysicsSystem::handleCollision(*body, ground, { 0, 0.01f, 0 }, underside, dt);

  // The ground stops the spin driving those corners into it, and none of it becomes linear velocity.
  expectNear("velocity", body->getVelocity(), { 0, 0, 0 });
  expectNear("angular velocity", body->getAngularVelocity(), { 0, 0, 0 });
}

namespace {
  // A unit square underside a box centered at the origin rests on, as a box-on-box manifold reports it.
  constexpr std::array<glm::vec3, 4> flatUnderside{
    glm::vec3{ -0.5f, -0.5f, -0.5f }, glm::vec3{ 0.5f, -0.5f, -0.5f },
    glm::vec3{ 0.5f, -0.5f, 0.5f }, glm::vec3{ -0.5f, -0.5f, 0.5f }
  };

  // Spin a flat box resting on a static ground still has after one contact response.
  glm::vec3 spinAfterRestingOnStaticGround(const glm::vec3& angularVelocity)
  {
    const auto scene = makeScene();
    const auto box = addObject(scene, "Box", { 0, 0, 0 });
    const auto body = addBody(box, false);
    const auto ground = addObject(scene, "Ground", { 0, -1, 0 });

    body->setAngularVelocity(angularVelocity);
    PhysicsSystem::handleCollision(*body, ground, { 0, 0.01f, 0 }, flatUnderside, dt);

    return body->getAngularVelocity();
  }
}

TEST(PhysicsIntegration, FrictionAtAContactIsBoundedByFrictionTimesTheImpulsePressingItThere)
{
  const auto slidAfterOneContact = [](const glm::vec3& velocity)
  {
    const auto scene = makeScene();
    const auto box = addObject(scene, "Box", { 0, 0, 0 });
    const auto body = addBody(box, false);
    const auto ground = addObject(scene, "Ground", { 0, -1, 0 });

    body->setFriction(0.5f);
    body->setVelocity(velocity);
    PhysicsSystem::handleCollision(*body, ground, { 0, 0.01f, 0 }, flatUnderside, dt);

    return std::pair{ body->getVelocity(), body->getAngularVelocity() };
  };

  // Landing at 0.1 per tick presses the box into the ground with 0.1 per unit mass, so friction can take up to
  // half of that off a slide of 0.3.
  const auto [fastVelocity, fastSpin] = slidAfterOneContact({ 0.3f, -0.1f, 0 });
  expectNear("fast slide", fastVelocity, { 0.25f, 0, 0 });

  // Resting on its face, the box is not tipped over by the drag at its underside.
  expectNear("spin", fastSpin, { 0, 0, 0 });

  // A slide slower than that limit is stopped outright rather than reversed.
  const auto [slowVelocity, slowSpin] = slidAfterOneContact({ 0.02f, -0.1f, 0 });
  expectNear("slow slide", slowVelocity, { 0, 0, 0 });
}

TEST(PhysicsIntegration, ASupportStopsTheSpinDrivingItsContactsIntoItButNotTheSpinAboutItsNormal)
{
  // Tipping about x drives the +z corners into the ground; turning about y moves every corner along it.
  // The unit box's inertia is the same on every axis, so the tipping part is removed outright.
  expectNear("angular velocity", spinAfterRestingOnStaticGround({ 4, 3, 0 }), { 0, 3, 0 });
}

TEST(PhysicsIntegration, ASpinLiftingAContactOffItsSupportIsLeftAlone)
{
  const std::array<glm::vec3, 2> edge{ glm::vec3{ 0.5f, -0.5f, 0.5f }, glm::vec3{ 0.5f, -0.5f, -0.5f } };

  const auto spinAfterRestingOnTheEdge = [&edge](const glm::vec3& angularVelocity)
  {
    const auto scene = makeScene();
    const auto box = addObject(scene, "Box", { 0, 0, 0 });
    const auto body = addBody(box, false);
    const auto ground = addObject(scene, "Ground", { 0, -1, 0 });

    body->setAngularVelocity(angularVelocity);
    PhysicsSystem::handleCollision(*body, ground, { 0, 0.01f, 0 }, edge, dt);

    return body->getAngularVelocity();
  };

  // Resting on its +x edge, spin about +z lifts that edge off the ground, as when the box falls back onto
  // its face or tips over a ledge on the far side of its center, so the contact leaves it alone. The
  // opposite spin drives the edge down into the ground and is stopped.
  expectNear("tipping away", spinAfterRestingOnTheEdge({ 0, 0, 5 }), { 0, 0, 5 });
  expectNear("driving into it", spinAfterRestingOnTheEdge({ 0, 0, -5 }), { 0, 0, 0 });
}

TEST(PhysicsIntegration, ARestingBodysLeftoverSpinIsBroughtExactlyToRest)
{
  // Spin about the normal only decays by the per-tick damping, which never reaches zero, and any spin at
  // all rewrites the rotation every tick. Compared exactly, since only exactly zero stops that rewrite.
  EXPECT_EQ(glm::length(spinAfterRestingOnStaticGround({ 0, 0.005f, 0 })), 0.0f);

  // Positive controls: a spin fast enough to see is kept, and so is a slow one against a contact from
  // above, which is not holding the body up.
  expectNear("visible spin", spinAfterRestingOnStaticGround({ 0, 0.05f, 0 }), { 0, 0.05f, 0 });

  const auto scene = makeScene();
  const auto box = addObject(scene, "Box", { 0, 0, 0 });
  const auto body = addBody(box, false);
  const auto ceiling = addObject(scene, "Ceiling", { 0, 1, 0 });

  const std::array<glm::vec3, 4> topside{
    glm::vec3{ -0.5f, 0.5f, -0.5f }, glm::vec3{ 0.5f, 0.5f, -0.5f },
    glm::vec3{ 0.5f, 0.5f, 0.5f }, glm::vec3{ -0.5f, 0.5f, 0.5f }
  };

  body->setAngularVelocity({ 0, 0.005f, 0 });
  PhysicsSystem::handleCollision(*body, ceiling, { 0, -0.01f, 0 }, topside, dt);

  expectNear("under a ceiling", body->getAngularVelocity(), { 0, 0.005f, 0 });
}

TEST(PhysicsIntegration, ABodyTurningWithItsSupportKeepsTheSpinTheyShare)
{
  // Stopped on a static ground: the -x corners are closing on it.
  expectNear("on static ground", spinAfterRestingOnStaticGround({ 0, 0, 2 }), { 0, 0, 0 });

  // On a support turning at the same rate, its surface moves with the corners and nothing is closing.
  const auto scene = makeScene();
  const auto box = addObject(scene, "Box", { 0, 0, 0 });
  const auto body = addBody(box, false);
  const auto support = addObject(scene, "Support", { 0, -1, 0 });
  const auto supportBody = addBody(support, false);

  body->setAngularVelocity({ 0, 0, 2 });
  supportBody->setAngularVelocity({ 0, 0, 2 });
  PhysicsSystem::handleCollision(*body, support, { 0, 0.01f, 0 }, flatUnderside, dt);

  expectNear("on a turning support", body->getAngularVelocity(), { 0, 0, 2 });
}

TEST(PhysicsIntegration, AContactUnderTheCenterIsNotSpunUpToChaseATurningSupport)
{
  const auto scene = makeScene();
  const auto ball = addObject(scene, "Ball", { 0, 0, 0 });
  const auto body = addBody(ball, false);
  const auto support = addObject(scene, "Support", { 1, -1, 0 });
  const auto supportBody = addBody(support, false);

  // A sphere's contact sits on the normal through its center, up to float noise, so spin cannot move that
  // point along the normal at all. The support turning under it still moves its own surface there. Its surface
  // also slides under the ball, which friction would rightly turn it with.
  body->setFriction(0.0f);
  supportBody->setAngularVelocity({ 0, 0, -2 });
  const std::array<glm::vec3, 1> underneath{ glm::vec3{ 1e-6f, -0.5f, 0 } };

  PhysicsSystem::handleCollision(*body, support, { 0, 0.01f, 0 }, underneath, dt);

  EXPECT_LT(glm::length(body->getAngularVelocity()), 1e-3f);
}

namespace {
  // The rotation of a unit box after one contact response resting its underside on a static ground.
  glm::vec3 rotationAfterRestingOn(const glm::vec3& rotation, const glm::vec3& minimumTranslationVector,
                                   const std::array<glm::vec3, 4>& contactPoints)
  {
    const auto scene = makeScene();
    const auto box = addObject(scene, "Box", { 0, 0, 0 });
    addBody(box, false);
    const auto ground = addObject(scene, "Ground", { 0, -1, 0 });

    transformOf(box)->setRotation(rotation);
    PhysicsSystem::handleCollision(*box->getComponent<RigidBody>(ComponentType::rigidBody), ground,
                                   minimumTranslationVector, contactPoints, dt);

    return transformOf(box)->getRotation();
  }
}

TEST(PhysicsIntegration, ABoxRestingOnAFaceWithinTheManifoldsToleranceIsLaidFlushWithIt)
{
  // Inside the tilt at which the manifold still reports all four corners, the support is centered and
  // nothing torques the box the rest of the way down; it used to stay there.
  const auto rotation = rotationAfterRestingOn({ -0.001f, 30, 0.016f }, { 0, 0.01f, 0 }, flatUnderside);

  fixtures::expectNear("up", localUpOf(rotation), { 0, 1, 0 }, 1e-5f);

  // Only the tilt is taken out: which way the box faces about the normal stays as it was.
  EXPECT_NEAR(rotation.y, 30.0f, 1e-3f);
}

TEST(PhysicsIntegration, ARealTiltOrAContactFromAboveIsNotLaidFlush)
{
  // Two degrees is past anything the manifold rounds to a whole face, so it is a real tilt, left alone.
  EXPECT_EQ(rotationAfterRestingOn({ 0, 0, 2 }, { 0, 0.01f, 0 }, flatUnderside), glm::vec3(0, 0, 2));

  // A face pressed down on from above is not what the body rests on.
  const std::array<glm::vec3, 4> topside{
    glm::vec3{ -0.5f, 0.5f, -0.5f }, glm::vec3{ 0.5f, 0.5f, -0.5f },
    glm::vec3{ 0.5f, 0.5f, 0.5f }, glm::vec3{ -0.5f, 0.5f, 0.5f }
  };
  EXPECT_EQ(rotationAfterRestingOn({ 0, 0, 0.016f }, { 0, -0.01f, 0 }, topside), glm::vec3(0, 0, 0.016f));

  // Positive control: the same small tilt resting on the face below is laid flush.
  fixtures::expectNear("up", localUpOf(rotationAfterRestingOn({ 0, 0, 0.016f }, { 0, 0.01f, 0 }, flatUnderside)),
                       { 0, 1, 0 }, 1e-5f);
}

TEST(PhysicsIntegration, AFlatBoxLandingSlightlyTiltedComesToRestAndStopsTurning)
{
  const auto scene = makeScene();

  const auto ground = addObject(scene, "Ground", { 0, 0, 0 }, { 5, 1, 5 });
  fixtures::addBoxCollider(ground);

  // A long, flat box tilted half a degree: too far off flat for all four underside corners to count as
  // touching, so it lands on an edge. It used to rock edge to edge through its flat pose indefinitely.
  const auto falling = addObject(scene, "Falling", { 0, 2, 0 }, { 2, 0.5f, 1 });
  fixtures::addBoxCollider(falling);
  const auto body = addBody(falling, true);
  transformOf(falling)->setRotation({ 0, 0, 0.5f });

  CollisionSystem collisionSystem;

  // Spin left about the vertical after the landing is only damped, so it takes a while to fall under the rest
  // threshold; the rotation is sampled once it has.
  glm::vec3 settledRotation{ 0 };
  for (int tick = 0; tick < 300; ++tick)
  {
    PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
    collisionSystem.fixedUpdate(*scene.objectManager, dt);

    if (tick == 200)
    {
      settledRotation = transformOf(falling)->getRotation();
    }
  }

  // Compared exactly: the complaint is a rotation that never stops changing, however slightly.
  EXPECT_EQ(transformOf(falling)->getRotation().x, settledRotation.x);
  EXPECT_EQ(transformOf(falling)->getRotation().y, settledRotation.y);
  EXPECT_EQ(transformOf(falling)->getRotation().z, settledRotation.z);
  EXPECT_EQ(glm::length(body->getAngularVelocity()), 0.0f);

  // Lying flat, not frozen at whatever small tilt the manifold already counts as a whole face.
  fixtures::expectNear("up", localUpOf(transformOf(falling)->getRotation()), { 0, 1, 0 }, 1e-5f);

  // Resting on the ground (its top is at 1, the box's half height 0.5), not stuck in or above it.
  EXPECT_NEAR(transformOf(falling)->getPosition().y, 1.5f, 0.1f);
}

TEST(PhysicsIntegration, TheSupportPointIsTheCenterOfMassProjectedOntoTheManifoldWhenItIsOverIt)
{
  const std::array<glm::vec3, 4> square{
    glm::vec3{ -0.5f, -0.5f, -0.5f }, glm::vec3{ 0.5f, -0.5f, 0.5f },
    glm::vec3{ 0.5f, -0.5f, -0.5f }, glm::vec3{ -0.5f, -0.5f, 0.5f }
  };

  // The corners are deliberately out of winding order: containment must not depend on it.
  expectNear("support point", PhysicsSystem::supportPoint({ 0.3f, 4, 0.2f }, { 0, 1, 0 }, square),
             { 0.3f, -0.5f, 0.2f });
}

TEST(PhysicsIntegration, TheSupportPointIsClampedToTheNearestManifoldEdgeWhenTheCenterOfMassOverhangsIt)
{
  const std::array<glm::vec3, 4> square{
    glm::vec3{ -0.5f, -0.5f, -0.5f }, glm::vec3{ 0.5f, -0.5f, -0.5f },
    glm::vec3{ 0.5f, -0.5f, 0.5f }, glm::vec3{ -0.5f, -0.5f, 0.5f }
  };

  expectNear("support point", PhysicsSystem::supportPoint({ 2, 1, 0.1f }, { 0, 1, 0 }, square),
             { 0.5f, -0.5f, 0.1f });

  const std::array<glm::vec3, 2> edge{ glm::vec3{ -0.5f, -0.5f, 0 }, glm::vec3{ 0.5f, -0.5f, 0 } };

  expectNear("edge support point", PhysicsSystem::supportPoint({ 0.2f, 0, 3 }, { 0, 1, 0 }, edge),
             { 0.2f, -0.5f, 0 });
}

TEST(PhysicsIntegration, AStallLongerThanTheStepCapHasItsBankedTimeDroppedNotCarriedForward)
{
  // A long stall (GC pause, breakpoint, OS scheduling hiccup) hands the run loop one huge dt. Before the
  // fix, ServerApp only capped how many ticks a single frame would replay - it never shed the leftover,
  // so the accumulator kept the whole backlog and the next several frames replayed it as one burst of
  // full-speed ticks (an object with a force applied every tick would move that many ticks' worth of
  // distance almost instantly). advance() must drop everything past the cap right away instead.
  constexpr float fixedDt = 1.0f / 50.0f;
  constexpr int maxSteps = 3;

  const auto plan = FixedTimestep::advance(0.0f, 5.0f, fixedDt, maxSteps);

  EXPECT_EQ(plan.steps, maxSteps);
  EXPECT_NEAR(plan.remainingAccumulator, 0.0f, 1e-5f);
}

TEST(PhysicsIntegration, AFrameWithinTheStepCapKeepsItsLeftoverForTheNextFrame)
{
  // Positive control for the test above: dropping the backlog only happens once the cap is actually hit.
  // A normal frame (well under the cap) must keep its sub-tick remainder exactly as before, or ticks
  // would drift out of sync with real time on every ordinary frame instead of only after a stall.
  constexpr float fixedDt = 1.0f / 50.0f;
  constexpr int maxSteps = 3;

  const auto plan = FixedTimestep::advance(0.0f, 0.05f, fixedDt, maxSteps);

  EXPECT_EQ(plan.steps, 2);
  EXPECT_NEAR(plan.remainingAccumulator, 0.01f, 1e-5f);
}
