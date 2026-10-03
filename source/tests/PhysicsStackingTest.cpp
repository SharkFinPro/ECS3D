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

namespace {
  // A unit box resting on a wide static ground, turning a full circle a second about the vertical. Returns its
  // spin two seconds later.
  glm::vec3 spinAfterTurningInPlace(const float friction)
  {
    const auto scene = makeScene();

    const auto ground = addObject(scene, "Ground", { 0, 0, 0 }, { 50, 1, 50 });
    fixtures::addBoxCollider(ground);

    const auto box = addObject(scene, "Box", { 0, 2, 0 });
    fixtures::addBoxCollider(box);
    const auto body = addBody(box, true);
    body->setFriction(friction);
    body->setAngularVelocity({ 0, 360, 0 });

    CollisionSystem collisionSystem;

    for (int tick = 0; tick < 20; ++tick)
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
      collisionSystem.fixedUpdate(*scene.objectManager, dt);
    }

    return body->getAngularVelocity();
  }

  // A unit ball set moving at three units per second along a wide static ground. Returns where it is and how fast
  // it moves ten seconds later.
  std::pair<glm::vec3, glm::vec3> ballAfterRolling(const float friction)
  {
    const auto scene = makeScene();

    const auto ground = addObject(scene, "Ground", { 0, 0, 0 }, { 50, 1, 50 });
    fixtures::addBoxCollider(ground);

    const auto ball = addObject(scene, "Ball", { 0, 2, 0 });
    fixtures::addSphereCollider(ball, 1.0f);
    const auto body = addBody(ball, true);
    body->setFriction(friction);
    body->setVelocity({ 3.0f * dt, 0, 0 });

    CollisionSystem collisionSystem;

    for (int tick = 0; tick < 100; ++tick)
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
      collisionSystem.fixedUpdate(*scene.objectManager, dt);
    }

    return { transformOf(ball)->getPosition(), body->getVelocity() };
  }
}

TEST(PhysicsIntegration, ABoxTurningInPlaceOnItsFaceIsStoppedByFriction)
{
  // The support point sits under the center, where turning about the normal does not slide it, so only the face
  // grinding on the ground can stop the box; it used to spin down by the angular damping alone.
  EXPECT_LT(glm::length(spinAfterTurningInPlace(0.5f)), 1.0f);

  // Positive control: frictionless, it is still turning most of a circle a second.
  EXPECT_GT(spinAfterTurningInPlace(0.0f).y, 200.0f);
}

TEST(PhysicsIntegration, ABallRollingAlongTheGroundComesToRest)
{
  // Friction soon has the ball rolling rather than sliding, and then no longer slows it: rolling resistance stops
  // it within a few of its own widths.
  const auto [position, velocity] = ballAfterRolling(0.5f);

  EXPECT_LT(position.x, 8.0f);
  EXPECT_LT(glm::length(velocity), 1e-3f);

  // Positive control: frictionless, it slides the whole way at the speed it started with.
  const auto [slidPosition, slidVelocity] = ballAfterRolling(0.0f);

  EXPECT_GT(slidPosition.x, 25.0f);
  EXPECT_GT(slidVelocity.x, 0.9f * 3.0f * dt);
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
