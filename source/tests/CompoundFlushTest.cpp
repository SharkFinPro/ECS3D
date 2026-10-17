#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "PhysicsTestHelpers.h"
#include "TestScene.h"
#include "objects/Object.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>

// A parent with a rigid body and no collider, carrying a box child with a rotation of its own, dropped on a floor.
TEST(CompoundFlush, ABoxChildWithItsOwnRotationSettlesFlushAndAtRest)
{
  const auto scene = fixtures::makeScene();

  const auto floor = addObject(scene, "Floor", { 0, 0, 0 }, { 50, 1, 50 });
  fixtures::addBoxCollider(floor);

  const auto owner = addObject(scene, "Owner", { 0, 1.2f, 0 });
  const auto body = physicsFixtures::addBody(owner, true);

  const auto part = addChildObject(scene, "Part", owner);
  fixtures::transformOf(part)->setRotation({ 0, 45, 10 });
  fixtures::addBoxCollider(part);

  CollisionSystem collisionSystem;
  for (int tick = 0; tick < 400; ++tick)
  {
    PhysicsSystem::fixedUpdate(*scene.objectManager, physicsFixtures::dt);
    collisionSystem.fixedUpdate(*scene.objectManager, physicsFixtures::dt);
  }

  // The child's face nearest to straight down is flat against the floor, which the owner's own axes do not show:
  // they are turned 45 degrees about y from the child's.
  const auto axes = glm::mat3_cast(glm::quat(glm::radians(fixtures::transformOf(part)->getRotation())));
  float flattest = 0.0f;
  for (int axis = 0; axis < 3; ++axis)
  {
    flattest = std::max(flattest, std::fabs(axes[axis].y));
  }
  EXPECT_GT(flattest, 0.9995f);

  EXPECT_LT(glm::length(body->getVelocity()), 1e-3f);
  EXPECT_LT(glm::length(body->getAngularVelocity()), 1.0f);
  // Positive control: it came down onto the floor rather than hanging where it started.
  EXPECT_LT(fixtures::transformOf(owner)->getPosition().y, 1.1f);
}
