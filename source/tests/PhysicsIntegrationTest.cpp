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
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <algorithm>
#include <array>
#include <cmath>
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

TEST(PhysicsIntegration, ATurnedBodySpinsThroughTheInertiaOfTheAxisNowLyingAlongTheTorque)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Turned", { 0, 0, 0 });
  const auto body = addBody(object, false);

  const auto transform = transformOf(object);
  transform->setScale({ 3, 1, 1 });
  transform->setRotation({ 0, 90, 0 });

  // The same push as above, but the long axis is turned from world x onto world z, the axis the torque
  // (0,0,-1) is about. The body now turns about one of its short axes, whose inertia is
  // (1/12) * 10 * 0.1 * (1 + 1) = 1/6, so it spins at 6 - not the 1.2 of its long axis, which is what
  // applying the body-frame tensor to a world-space torque gives.
  PhysicsSystem::applyForce(*body, *transform, { 1, 0, 0 }, { 0, 1, 0 });

  expectNear("angular velocity", body->getAngularVelocity(), { 0, 0, -6 });
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
  PhysicsSystem::applyForce(*body, *transform, { 1, 0, 0 }, { 0, 1, 0 });

  const auto angularVelocity = body->getAngularVelocity();
  EXPECT_TRUE(std::isfinite(angularVelocity.x));
  EXPECT_TRUE(std::isfinite(angularVelocity.y));
  EXPECT_TRUE(std::isfinite(angularVelocity.z));

  // Positive control: a normal body at the same lever arm does pick up spin, so the assertion above is
  // catching the degenerate tensor rather than a guard that swallows every off-centre push.
  const auto normalObject = addObject(scene, "Normal", { 0, 0, 0 });
  const auto normalBody = addBody(normalObject, false);
  PhysicsSystem::applyForce(*normalBody, *transformOf(normalObject), { 1, 0, 0 }, { 0, 1, 0 });

  expectNear("angular velocity", normalBody->getAngularVelocity(), { 0, 0, -6 });
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
  addBody(falling, true);

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
    collisionSystem.fixedUpdate(*scene.objectManager);

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
    collisionSystem.fixedUpdate(*scene.objectManager);

    maxAngularSpeed = std::max(maxAngularSpeed, glm::length(body->getAngularVelocity()));
  }

  EXPECT_GT(maxAngularSpeed, 0.1f);
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
    collisionSystem.fixedUpdate(*scene.objectManager);

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
    collisionSystem.fixedUpdate(*scene.objectManager);

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
    collisionSystem.fixedUpdate(*scene.objectManager);

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
      collisionSystem.fixedUpdate(*scene.objectManager);

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
  // a body moves. The contact is placed along the normal so the impulse produces no torque of its own.
  PhysicsSystem::handleCollision(*leftBody, right, { 1, 0, 0 }, { 2, 0, 0 });

  expectNear("left position", transformOf(left)->getPosition(), { 1, 0, 0 });
  expectNear("right position", transformOf(right)->getPosition(), { -1, 0, 0 });

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

  PhysicsSystem::handleCollision(*leftBody, right, { 1, 0, 0 }, { 2, 0, 0 });

  // Still pushed apart - an overlap is an overlap - but no impulse to either body, because they are
  // already separating and adding one would fling apart two bodies that were resolving themselves.
  expectNear("left position", transformOf(left)->getPosition(), { 1, 0, 0 });
  expectNear("left velocity", leftBody->getVelocity(), { 1, 0, 0 });
  expectNear("right velocity", right->getComponent<RigidBody>(ComponentType::rigidBody)->getVelocity(),
             { -1, 0, 0 });
}

TEST(PhysicsIntegration, ASpinningBoxResolvedAcrossAManifoldIsNotFlungByItsOwnSpin)
{
  const auto scene = makeScene();

  const auto box = addObject(scene, "Box", { 0, 0, 0 });
  const auto body = addBody(box, false);
  const auto ground = addObject(scene, "Ground", { 0, -1, 0 });

  // Angular velocity is in degrees per second and linear velocity in units per tick. Read as the same
  // unit, this spin has two underside corners closing on the ground at 90 units per tick, which a solve
  // over the manifold turned into a linear kick of that order.
  body->setAngularVelocity({ 180, 0, 0 });

  const std::array<glm::vec3, 4> underside{
    glm::vec3{ -0.5f, -0.5f, -0.5f }, glm::vec3{ 0.5f, -0.5f, -0.5f },
    glm::vec3{ 0.5f, -0.5f, 0.5f }, glm::vec3{ -0.5f, -0.5f, 0.5f }
  };

  PhysicsSystem::handleCollision(*body, ground, { 0, 0.01f, 0 }, underside);

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
    PhysicsSystem::handleCollision(*body, ground, { 0, 0.01f, 0 }, flatUnderside);

    return body->getAngularVelocity();
  }
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
    PhysicsSystem::handleCollision(*body, ground, { 0, 0.01f, 0 }, edge);

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
  PhysicsSystem::handleCollision(*body, ceiling, { 0, -0.01f, 0 }, topside);

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
  PhysicsSystem::handleCollision(*body, support, { 0, 0.01f, 0 }, flatUnderside);

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
  // point along the normal at all. The support turning under it still moves its own surface there.
  supportBody->setAngularVelocity({ 0, 0, -2 });
  const std::array<glm::vec3, 1> underneath{ glm::vec3{ 1e-6f, -0.5f, 0 } };

  PhysicsSystem::handleCollision(*body, support, { 0, 0.01f, 0 }, underneath);

  EXPECT_LT(glm::length(body->getAngularVelocity()), 1e-3f);
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

  glm::vec3 settledRotation{ 0 };
  for (int tick = 0; tick < 200; ++tick)
  {
    PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
    collisionSystem.fixedUpdate(*scene.objectManager);

    if (tick == 150)
    {
      settledRotation = transformOf(falling)->getRotation();
    }
  }

  // Compared exactly: the complaint is a rotation that never stops changing, however slightly.
  EXPECT_EQ(transformOf(falling)->getRotation().x, settledRotation.x);
  EXPECT_EQ(transformOf(falling)->getRotation().y, settledRotation.y);
  EXPECT_EQ(transformOf(falling)->getRotation().z, settledRotation.z);
  EXPECT_EQ(glm::length(body->getAngularVelocity()), 0.0f);

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
