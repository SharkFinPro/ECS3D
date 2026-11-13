#include <gtest/gtest.h>

#include "TestScene.h"
#include "CollisionSystem.h"
#include "FixedTimestep.h"
#include "PhysicsSystem.h"
#include "objects/Object.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "PhysicsTestHelpers.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
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

  // Sixty ticks: enough to land at tick eight and settle.
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
  EXPECT_NEAR(lowest, 2.0f, 0.05f);
  EXPECT_NEAR(highest, 2.0f, 0.05f);
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

  // On top of the ground (both half extents are 1) and still over its middle, not sunk in or slid off.
  fixtures::expectNear("resting position", transformOf(falling)->getPosition(), { 0, 2, 0 }, 0.05f);
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

  const auto ground = addObject(scene, "Ground", { 0, 0, 0 }, { 5, 1, 5 });
  fixtures::addBoxCollider(ground);

  // Tilted 25 degrees, it lands on one edge with its center of mass over the ground, so the support at that
  // edge turns it down onto its face. The spin a support gives used to be thousands of times too weak, and
  // after these thirty ticks the box was still leaning more than ten degrees.
  const auto falling = addObject(scene, "Falling", { 0, 5, 0 });
  fixtures::addBoxCollider(falling);
  addBody(falling, true);
  transformOf(falling)->setRotation({ 0, 0, 25 });

  CollisionSystem collisionSystem;

  // Three seconds at the server's tick.
  constexpr float serverTick = 1.0f / 50.0f;
  for (int tick = 0; tick < 150; ++tick)
  {
    PhysicsSystem::fixedUpdate(*scene.objectManager, serverTick);
    collisionSystem.fixedUpdate(*scene.objectManager, serverTick);
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

  // The same box as above, tipping over the ledge, 13 ticks of the server's tick in. Returns where the point of its
  // underside that started on the ledge's edge is then, and how far the box has turned.
  std::pair<glm::vec3, float> ledgePivotAfterTipping(const float friction)
  {
    const auto scene = makeScene();

    const auto ground = addObject(scene, "Ground", { 0, 0, 0 }, { 3, 1, 3 });
    fixtures::addBoxCollider(ground);

    const auto box = addObject(scene, "Box", { 3.3f, 2, 0 });
    fixtures::addBoxCollider(box);
    addBody(box, true)->setFriction(friction);

    CollisionSystem collisionSystem;

    constexpr float serverTick = 1.0f / 50.0f;
    for (int tick = 0; tick < 13; ++tick)
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, serverTick);
      collisionSystem.fixedUpdate(*scene.objectManager, serverTick);
    }

    const auto& transform = *transformOf(box);
    const float tilt = glm::degrees(std::acos(std::clamp(localUpOf(transform.getRotation()).y, -1.0f, 1.0f)));

    return { worldPointOf(transform, { -0.3f, -1, 0 }), tilt };
  }
}

TEST(PhysicsIntegration, ABoxTippingOffALedgeTurnsAboutTheEdgeRatherThanSlidingOverIt)
{
  // Friction at the edge holds the underside near there while the box turns about it. Not exactly: the contact
  // the narrow phase reports shifts as the box tips.
  const glm::vec3 edge{ 3, 1, 0 };
  const auto [pivot, tilt] = ledgePivotAfterTipping(0.5f);

  EXPECT_GT(tilt, 10.0f);
  EXPECT_LT(glm::distance(pivot, edge), 0.1f);

  // Positive control: without friction the same point slides back along the ledge as the box turns.
  const auto [slidingPivot, slidingTilt] = ledgePivotAfterTipping(0.0f);

  EXPECT_GT(slidingTilt, 10.0f);
  EXPECT_LT(slidingPivot.x, edge.x - 0.1f);
}

namespace {
  // A unit box dropped a unit onto a wide static ground while moving sideways at three units per second, run at the
  // server's tick for three seconds.
  constexpr float serverTick = 1.0f / 50.0f;
  constexpr float sidewaysSpeed = 3.0f;
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
    body->setVelocity({ sidewaysSpeed * serverTick, 0, 0 });

    CollisionSystem collisionSystem;

    for (int tick = 0; tick < 150; ++tick)
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, serverTick);
      collisionSystem.fixedUpdate(*scene.objectManager, serverTick);
    }

    return { transformOf(box)->getPosition(), body->getVelocity(), localUpOf(transformOf(box)->getRotation()) };
  }
}

TEST(PhysicsIntegration, ABoxLandingWithSidewaysSpeedStopsSlidingWithinAShortDistance)
{
  // The box falls one unit, which takes sqrt(2 / (g * unitsPerMeter)) seconds, so it reaches the ground that much of
  // its sideways speed along. Friction then stops it within v^2 / (2 * friction * g * unitsPerMeter). The window is
  // the landing point and that slide, so it catches a box that loses its slide before landing or keeps it, though
  // not one that sticks at the landing point, which the slide is too short to tell apart.
  const float acceleration = 9.81f * PhysicsSystem::unitsPerMeter;
  const float landingX = sidewaysSpeed * std::sqrt(2.0f / acceleration);
  const float slide = sidewaysSpeed * sidewaysSpeed / (2.0f * 0.5f * acceleration);
  const auto landed = landSliding(0.5f);

  EXPECT_GT(landed.position.x, landingX - 0.05f);
  EXPECT_LT(landed.position.x, landingX + slide + 0.1f);
  EXPECT_LT(glm::length(glm::vec2(landed.velocity.x, landed.velocity.z)), 1e-4f);
  fixtures::expectNear("up", landed.up, { 0, 1, 0 }, 1e-3f);
  EXPECT_NEAR(landed.position.y, 2.0f, 0.05f);

  // Positive control: on a frictionless contact it keeps all three seconds of its slide.
  EXPECT_GT(landSliding(0.0f).position.x, 0.9f * sidewaysSpeed * 3.0f);
}


namespace {
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
  glm::vec3 settledSpin{ 1 };
  for (int tick = 0; tick < 300; ++tick)
  {
    PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
    collisionSystem.fixedUpdate(*scene.objectManager, dt);

    if (tick == 200)
    {
      settledRotation = transformOf(falling)->getRotation();
      settledSpin = body->getAngularVelocity();
    }
  }

  // Both ends of the last hundred ticks are at rest, and a body at rest has exactly no spin and is not rewritten,
  // so its rotation is compared exactly: the complaint is a rotation that never stops changing, however slightly.
  EXPECT_EQ(glm::length(settledSpin), 0.0f);
  EXPECT_EQ(glm::length(body->getAngularVelocity()), 0.0f);
  EXPECT_EQ(transformOf(falling)->getRotation().x, settledRotation.x);
  EXPECT_EQ(transformOf(falling)->getRotation().y, settledRotation.y);
  EXPECT_EQ(transformOf(falling)->getRotation().z, settledRotation.z);

  // Lying flat, not frozen at whatever small tilt the manifold already counts as a whole face.
  fixtures::expectNear("up", localUpOf(transformOf(falling)->getRotation()), { 0, 1, 0 }, 1e-5f);

  // Resting on the ground (its top is at 1, the box's half height 0.5), not stuck in or above it.
  EXPECT_NEAR(transformOf(falling)->getPosition().y, 1.5f, 0.1f);
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
