#include <gtest/gtest.h>

#include "TestScene.h"
#include "PhysicsTestHelpers.h"
#include "CollisionSystem.h"
#include "FixedTimestep.h"
#include "PhysicsSystem.h"
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
#include <utility>

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

TEST(PhysicsIntegration, AChildOfATurnedParentIsLaidFlushWithoutTakingOnTheParentsTurn)
{
  const auto scene = makeScene();
  const auto parent = addObject(scene, "Parent", { 0, 0, 0 });
  transformOf(parent)->setRotation({ 0, 30, 0 });

  const auto box = addChildObject(scene, "Box", parent);
  const auto body = addBody(box, false);
  transformOf(box)->setRotation({ 0, 0, 0.016f });
  const auto ground = addObject(scene, "Ground", { 0, -1, 0 });

  PhysicsSystem::handleCollision(*body, ground, { 0, 0.01f, 0 }, flatUnderside, dt);

  fixtures::expectNear("up", localUpOf(transformOf(box)->getRotation()), { 0, 1, 0 }, 1e-5f);
  EXPECT_NEAR(transformOf(box)->getRotation().y, 30.0f, 1e-3f);
  EXPECT_NEAR(transformOf(box)->getLocalRotation().y, 0.0f, 1e-3f);
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
