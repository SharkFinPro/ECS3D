#include <gtest/gtest.h>

#include "PhysicsSystem.h"
#include "PhysicsTestHelpers.h"
#include "TestScene.h"
#include "objects/Object.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/SphereCollider.h"

#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

namespace {
  constexpr float centerHeight = 5.0f;
  constexpr float spinPerTick = 0.2f;
  constexpr float sinkSpeed = 0.05f;

  // A body whose only collider is a ball of the given radius, held centerHeight above the body's own position and
  // spinning about z while its bottom moves with the spin, so the contact there is rolling rather than sliding.
  // Returns how much of that spin the contact's rolling resistance took, in degrees per second.
  float spinRemovedByRolling(const float radius)
  {
    const auto scene = fixtures::makeScene();

    const auto ground = addObject(scene, "Ground", { 0, -10, 0 });
    fixtures::addBoxCollider(ground);

    // The separation moves the owner up by the translation vector, which leaves it at the origin when it counts.
    const auto owner = addObject(scene, "Owner", { 0, -0.01f, 0 });
    const auto body = physicsFixtures::addBody(owner, false);

    const auto ball = addChildObject(scene, "Ball", owner);
    fixtures::transformOf(ball)->setPosition({ 0, centerHeight, 0 });
    const auto collider = fixtures::addSphereCollider(ball, radius);

    const float arm = centerHeight - radius;
    const float spin = glm::degrees(spinPerTick) / physicsFixtures::dt;
    body->setAngularVelocity({ 0, 0, spin });
    body->setVelocity({ spinPerTick * arm, -sinkSpeed, 0 });

    PhysicsSystem::handleCollision(*body, collider, ground, { 0, 0.01f, 0 }, glm::vec3(0, arm, 0),
                                   physicsFixtures::dt);

    return spin - body->getAngularVelocity().z;
  }
}

TEST(CompoundRollingArm, ABallOffsetFromItsOwnerRollsAboutItsOwnRadius)
{
  const float small = spinRemovedByRolling(1.0f);
  const float large = spinRemovedByRolling(2.0f);

  // Positive control: the contact did take some of the spin.
  ASSERT_GT(small, 0.0f);

  // The arm is each ball's radius, so twice the radius takes about twice the spin. Measured from the owner to the
  // contact point instead, it would be 4 and 3, and the larger ball would take less.
  EXPECT_GT(large / small, 1.7f);
  EXPECT_LT(large / small, 2.2f);
}
