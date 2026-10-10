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

namespace {
  // A body of mass 10 lands at unit speed on one of mass 0.1 resting on a support, touching along normal, the
  // way a heavy box comes down on a small one at an angle in a pile.
  struct Squeeze {
    glm::vec3 lowerVelocity;
    glm::vec3 lowerSpin;
    float closingAfter;
  };

  Squeeze landObliquely(const glm::vec3& normal, const float friction)
  {
    const auto scene = makeScene();

    const auto lower = addObject(scene, "Lower", { 0, 0, 0 });
    const auto lowerBody = addBody(lower, false);
    const auto upper = addObject(scene, "Upper", 2.0f * normal);
    const auto upperBody = addBody(upper, false);

    lowerBody->setMass(0.1f);
    lowerBody->setFriction(friction);
    lowerBody->setFalling(false);
    upperBody->setMass(10.0f);
    upperBody->setFriction(friction);
    upperBody->setVelocity({ 0, -1, 0 });

    PhysicsSystem::handleCollision(*upperBody, lower, 0.01f * normal, normal, dt);

    return { lowerBody->getVelocity(), lowerBody->getAngularVelocity(),
             -glm::dot(upperBody->getVelocity() - lowerBody->getVelocity(), normal) };
  }
}

TEST(PhysicsIntegration, ABodyLandingOnALightRestingOneInsideItsFrictionConeLeavesItWhereItIs)
{
  // Seventeen degrees off vertical, inside the cone a friction of 0.5 holds. The light body's support takes the
  // whole push, as its friction would: it is neither shot sideways nor spun, and the heavy one stops on it. Its
  // horizontal share was once all the light body's, which at a hundred to one sent it off at six units a tick.
  const auto squeeze = landObliquely(glm::normalize(glm::vec3(0.3f, 1, 0)), 0.5f);

  expectNear("lower velocity", squeeze.lowerVelocity, { 0, 0, 0 });
  expectNear("lower spin", squeeze.lowerSpin, { 0, 0, 0 });
  EXPECT_NEAR(squeeze.closingAfter, 0.0f, 1e-4f);
}

TEST(PhysicsIntegration, ABodyLandingOnALightRestingOneOutsideItsFrictionConeSqueezesItOutByTheSlope)
{
  // Frictionless, so nothing holds the light body sideways: it moves out of the way just fast enough for the
  // heavy one to stop closing on it - set by the slope of the contact, not by the hundred-to-one masses - and
  // its support still keeps it from turning.
  const auto squeeze = landObliquely(glm::normalize(glm::vec3(1, 1, 0)), 0.0f);

  EXPECT_NEAR(squeeze.closingAfter, 0.0f, 1e-4f);
  expectNear("lower velocity", squeeze.lowerVelocity, { -0.98039f, 0, 0 });
  expectNear("lower spin", squeeze.lowerSpin, { 0, 0, 0 });
}

TEST(PhysicsIntegration, AHeavyBodySlidingOverALightRestingOneDoesNotSpinItOrDragItAlong)
{
  const auto scene = makeScene();

  const auto lower = addObject(scene, "Lower", { 0, 0, 0 });
  const auto lowerBody = addBody(lower, false);
  const auto upper = addObject(scene, "Upper", { 0.5f, 2, 0 });
  const auto upperBody = addBody(upper, false);

  lowerBody->setMass(1.0f);
  lowerBody->setFalling(false);
  upperBody->setMass(100.0f);
  upperBody->setVelocity({ 1, -0.1f, 0 });

  // Off the lower body's center, where a drag sized for the heavy body used to turn the light one thousands of
  // degrees a second.
  PhysicsSystem::handleCollision(*upperBody, lower, { 0, 0.01f, 0 }, glm::vec3{ 0.5f, 1, 0 }, dt);

  // The support holds it against the drag, up to what friction there can take - here the whole of it, since the
  // heavy body's weight presses it down.
  expectNear("lower velocity", lowerBody->getVelocity(), { 0, 0, 0 });
  expectNear("lower spin", lowerBody->getAngularVelocity(), { 0, 0, 0 });

  // Positive control: the heavy body is slowed by friction times the weight it presses down with.
  expectNear("upper velocity", upperBody->getVelocity(), { 0.95f, 0, 0 });
}

namespace {
  // A unit ball dropped half a unit off center onto another resting on a wide static ground, both as heavy as
  // their volume, the upper one upperMass. Returns the furthest the lower ball moves toward the side the upper
  // one falls off, and where it ends up, over three seconds of the server's ticks.
  std::pair<float, float> lowerBallAfterTheUpperRollsOff(const float upperMass)
  {
    const auto scene = makeScene();

    const auto ground = addObject(scene, "Ground", { 0, -9, 0 }, { 100, 10, 100 });
    fixtures::addBoxCollider(ground);

    const auto lower = addObject(scene, "Lower", { 0, 2, 0 });
    fixtures::addSphereCollider(lower, 1.0f);
    addBody(lower, true)->setMass(4.19f);

    const auto upper = addObject(scene, "Upper", { 0.5f, 5, 0 });
    fixtures::addSphereCollider(upper, 1.0f);
    addBody(upper, true)->setMass(upperMass);

    constexpr float serverDt = 1.0f / 50.0f;
    CollisionSystem collisionSystem;

    float furthestToward = std::numeric_limits<float>::lowest();
    for (int tick = 0; tick < 150; ++tick)
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, serverDt);
      collisionSystem.fixedUpdate(*scene.objectManager, serverDt);

      furthestToward = std::max(furthestToward, transformOf(lower)->getPosition().x);
    }

    return { furthestToward, transformOf(lower)->getPosition().x };
  }
}

TEST(PhysicsIntegration, ABallRollingOffAnotherPushesItAwayRatherThanPullingItAlong)
{
  // The contact pushes the lower ball away from the side the upper one leaves by, while friction drags it that
  // way; its support holds the difference. Held as separate amounts, the two used up its grip between them and
  // the drag of a heavy ball pulled the lower one after it.
  for (const float upperMass : { 4.19f, 20.0f })
  {
    SCOPED_TRACE(testing::Message() << "upper mass " << upperMass);

    const auto [furthestToward, finalX] = lowerBallAfterTheUpperRollsOff(upperMass);

    EXPECT_LT(furthestToward, 0.01f);
    EXPECT_LT(finalX, 0.0f);
  }
}

TEST(PhysicsIntegration, TwoBodiesMeetingSlowlyDoNotBounce)
{
  const auto velocitiesAfterMeeting = [](const float unitsPerSecond)
  {
    const auto scene = makeScene();
    const auto left = addObject(scene, "Left", { 0, 0, 0 });
    const auto leftBody = addBody(left, false);
    const auto right = addObject(scene, "Right", { 2, 0, 0 });
    const auto rightBody = addBody(right, false);

    leftBody->setVelocity({ unitsPerSecond * dt, 0, 0 });
    PhysicsSystem::handleCollision(*leftBody, right, { -0.01f, 0, 0 }, glm::vec3{ 1, 0, 0 }, dt);

    return std::pair{ leftBody->getVelocity() / dt, rightBody->getVelocity() / dt };
  };

  // At a unit a second - about the speed one tick of gravity presses bodies in a pile together - they move on
  // together rather than trading it, or a resting pile would keep hopping.
  const auto [slowLeft, slowRight] = velocitiesAfterMeeting(1.0f);
  expectNear("slow left", slowLeft, { 0.5f, 0, 0 });
  expectNear("slow right", slowRight, { 0.5f, 0, 0 });

  // Positive control: a real impact still trades the velocities of equal masses.
  const auto [fastLeft, fastRight] = velocitiesAfterMeeting(30.0f);
  expectNear("fast left", fastLeft, { 0, 0, 0 });
  expectNear("fast right", fastRight, { 30.0f, 0, 0 });
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
  struct ZeroMtvResult
  {
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec3 angularVelocity;
  };

  // A falling, spinning body touching a ground, answered through either overload.
  ZeroMtvResult respondTo(const glm::vec3& minimumTranslationVector, const bool manifold)
  {
    const auto scene = makeScene();
    const auto box = addObject(scene, "Box", { 0, 0, 0 });
    const auto body = addBody(box, false);
    const auto ground = addObject(scene, "Ground", { 0, -1, 0 });

    body->setVelocity({ 0, -1, 0 });
    body->setAngularVelocity({ 10, 0, 0 });

    if (manifold)
    {
      PhysicsSystem::handleCollision(*body, ground, minimumTranslationVector, flatUnderside, dt);
    }
    else
    {
      PhysicsSystem::handleCollision(*body, ground, minimumTranslationVector, glm::vec3{ 0, -0.5f, 0 }, dt);
    }

    return { transformOf(box)->getPosition(), body->getVelocity(), body->getAngularVelocity() };
  }

  void expectUnchangedAndFinite(const ZeroMtvResult& result)
  {
    for (int axis = 0; axis < 3; ++axis)
    {
      EXPECT_TRUE(std::isfinite(result.position[axis]));
      EXPECT_TRUE(std::isfinite(result.velocity[axis]));
      EXPECT_TRUE(std::isfinite(result.angularVelocity[axis]));
    }

    expectNear("position", result.position, { 0, 0, 0 });
    expectNear("velocity", result.velocity, { 0, -1, 0 });
    expectNear("angular velocity", result.angularVelocity, { 10, 0, 0 });
  }
}

TEST(PhysicsIntegration, AZeroTranslationVectorLeavesTheBodyUntouchedInBothOverloads)
{
  expectUnchangedAndFinite(respondTo({ 0, 0, 0 }, false));
  expectUnchangedAndFinite(respondTo({ 0, 0, 0 }, true));

  // A real translation vector does answer the contact.
  EXPECT_GT(respondTo({ 0, 0.01f, 0 }, false).velocity.y, -1.0f);
  EXPECT_GT(respondTo({ 0, 0.01f, 0 }, true).velocity.y, -1.0f);
}
