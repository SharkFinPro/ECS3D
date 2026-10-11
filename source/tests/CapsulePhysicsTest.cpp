#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "PhysicsTestHelpers.h"
#include "TestScene.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/CapsuleCollider.h"

#include <algorithm>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <memory>

namespace {
  using namespace physicsFixtures;
  using fixtures::addObject;
  using fixtures::makeScene;
  using fixtures::transformOf;

  // The unit box [-1,1]^3 at the origin, with no rigid body: static geometry whose top face is y = 1.
  void addGround(const fixtures::Scene& scene)
  {
    fixtures::addBoxCollider(addObject(scene, "Ground", { 0, 0, 0 }));
  }

  void run(const fixtures::Scene& scene, const int ticks)
  {
    CollisionSystem collisionSystem;

    for (int tick = 0; tick < ticks; ++tick)
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
      collisionSystem.fixedUpdate(*scene.objectManager, dt);
    }
  }
}

TEST(CapsulePhysics, ACapsuleDroppedLyingFlatRestsOnTheBoxWithoutDriftingOrSpinning)
{
  const auto scene = makeScene();
  addGround(scene);

  const auto falling = addObject(scene, "Falling", { 0, 3, 0 });
  transformOf(falling)->setRotation({ 0, 0, 90 });
  fixtures::addCapsuleCollider(falling, 0.5f, 2.0f);
  const auto body = addBody(falling, true);

  run(scene, 200);

  const auto position = transformOf(falling)->getPosition();

  // Resting on the top face (y = 1) by its radius, and not carried sideways or through the box.
  EXPECT_NEAR(position.y, 1.5f, 0.1f);
  EXPECT_NEAR(position.x, 0.0f, 0.1f);
  EXPECT_NEAR(position.z, 0.0f, 0.1f);
  EXPECT_LT(glm::length(body->getAngularVelocity()), 1.0f);

  // Still lying along x: the axis has not rolled up off the box or yawed round.
  const auto axis = localUpOf(transformOf(falling)->getRotation());
  EXPECT_NEAR(axis.y, 0.0f, 0.1f);
  EXPECT_NEAR(glm::abs(axis.x), 1.0f, 0.1f);
}

TEST(CapsulePhysics, ACapsuleDroppedOnItsEndIsSupportedByTheBoxAndComesToRest)
{
  // Balanced on its end is not a stable pose, so it may stay up or tip over; what must hold is that the box
  // carries it and it stops moving, standing or lying.
  for (const glm::vec3 rotation : { glm::vec3(0, 0, 0), glm::vec3(0, 0, 10) })
  {
    SCOPED_TRACE(::testing::Message() << "rotation z " << rotation.z);

    const auto scene = makeScene();
    addGround(scene);

    const auto falling = addObject(scene, "Falling", { 0, 3, 0 });
    transformOf(falling)->setRotation(rotation);
    const auto capsule = fixtures::addCapsuleCollider(falling, 0.5f, 2.0f);
    const auto body = addBody(falling, true);

    run(scene, 300);

    glm::vec3 previous = transformOf(falling)->getPosition();
    float fastest = 0.0f;
    CollisionSystem collisionSystem;
    for (int tick = 0; tick < 50; ++tick)
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
      collisionSystem.fixedUpdate(*scene.objectManager, dt);

      const auto position = transformOf(falling)->getPosition();
      fastest = std::max(fastest, glm::length(position - previous));
      previous = position;
    }

    // Its lowest point is on the top face (y = 1): not sunk into the box and not hovering above it.
    const float lowest = capsule->getBoundingBox().minY;
    EXPECT_GT(lowest, 0.9f);
    EXPECT_LT(lowest, 1.1f);

    EXPECT_LT(fastest, 0.05f);
    EXPECT_LT(glm::length(body->getAngularVelocity()), 30.0f);
  }
}

TEST(CapsulePhysics, WithNoGroundTheSameCapsuleKeepsFalling)
{
  // Positive control for the two tests above: with no ground the same capsule keeps falling, so the rest
  // height they measure comes from the box and not from the capsule hanging in place.
  const auto scene = makeScene();

  const auto falling = addObject(scene, "Falling", { 0, 3, 0 });
  fixtures::addCapsuleCollider(falling, 0.5f, 2.0f);
  addBody(falling, true);

  run(scene, 50);

  EXPECT_LT(transformOf(falling)->getPosition().y, 2.0f);
}
