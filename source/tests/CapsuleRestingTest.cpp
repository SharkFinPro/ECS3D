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

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <cmath>
#include <cstdio>
#include <memory>

namespace {
  using namespace physicsFixtures;
  using fixtures::addObject;
  using fixtures::makeScene;
  using fixtures::transformOf;

  struct Lying {
    fixtures::Scene scene = makeScene();
    std::shared_ptr<Object> object;
    std::shared_ptr<RigidBody> body;
    std::shared_ptr<CapsuleCollider> capsule;
    CollisionSystem collisions;

    Lying(const glm::vec3& position, const glm::vec3& rotation, const glm::vec3& velocity,
          const glm::vec3& colliderRotation = glm::vec3(0))
    {
      // A slab whose top face is y = 1.
      const auto ground = addObject(scene, "Ground", { 0, 0, 0 });
      transformOf(ground)->setScale({ 20, 1, 20 });
      fixtures::addBoxCollider(ground);

      object = addObject(scene, "Capsule", position);
      transformOf(object)->setRotation(rotation);
      capsule = fixtures::addCapsuleCollider(object, 0.5f, 2.0f);
      capsule->setRotation(colliderRotation);
      body = addBody(object, true);
      body->setVelocity(velocity);
    }

    void run(const int ticks)
    {
      for (int tick = 0; tick < ticks; ++tick)
      {
        PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
        collisions.fixedUpdate(*scene.objectManager, dt);
      }
    }

    // Degrees between the capsule's axis and the horizontal plane.
    [[nodiscard]] float tiltDegrees() const
    {
      const auto core = capsule->getSegment();
      const auto axis = glm::normalize(core.end - core.start);
      return glm::degrees(std::asin(std::fabs(axis.y)));
    }
  };
}

namespace {
  // Degrees. Far inside the half-degree the capsule rested off flush before it was laid flat.
  constexpr float flushToleranceDegrees = 1e-3f;
  constexpr float restSpeed = 1e-5f;

  void expectLyingFlushAtRest(const Lying& lying)
  {
    EXPECT_LT(lying.tiltDegrees(), flushToleranceDegrees);
    EXPECT_NEAR(transformOf(lying.object)->getPosition().y, 1.5f, 1e-3f);
    EXPECT_LT(glm::length(lying.body->getVelocity()), restSpeed);
  }
}

TEST(CapsuleResting, ACapsuleSlidingAlongItsAxisRestsFlush)
{
  for (const auto& rotation : { glm::vec3(0, 0, 90), glm::vec3(90, 0, 0) })
  {
    SCOPED_TRACE(::testing::Message() << "rotation " << rotation.x << " " << rotation.z);

    const auto velocity = rotation.z > 0.0f ? glm::vec3(0.3f, 0, 0) : glm::vec3(0, 0, 0.3f);
    Lying lying({ 0, 1.6f, 0 }, rotation, velocity);

    lying.run(400);

    expectLyingFlushAtRest(lying);
  }
}

TEST(CapsuleResting, ACapsuleDroppedWithASmallTiltRestsFlush)
{
  for (const float rotationZ : { 90.5f, 92.0f, 88.0f })
  {
    SCOPED_TRACE(::testing::Message() << "rotation z " << rotationZ);

    Lying lying({ 0, 2.0f, 0 }, { 0, 0, rotationZ }, { 0, 0, 0 });
    // Positive control: it starts off flush by the tilt it was dropped with.
    EXPECT_NEAR(lying.tiltDegrees(), std::fabs(rotationZ - 90.0f), 1e-3f);

    lying.run(400);

    expectLyingFlushAtRest(lying);
  }
}

TEST(CapsuleResting, ACapsuleRollingSidewaysStaysFlushAndComesToRest)
{
  Lying lying({ 0, 1.6f, 0 }, { 0, 0, 90 }, { 0, 0, 0.3f });

  lying.run(400);

  expectLyingFlushAtRest(lying);
}

TEST(CapsuleResting, ACapsuleTiltedWellPastTheFlushLimitIsNotSnappedOnContact)
{
  Lying lying({ 0, 2.0f, 0 }, { 0, 0, 110 }, { 0, 0, 0 });

  lying.run(3);

  // It has landed on a cap by now, and still stands at the tilt it was dropped with.
  EXPECT_LT(transformOf(lying.object)->getPosition().y, 1.8f);
  EXPECT_NEAR(lying.tiltDegrees(), 20.0f, 0.5f);

  // Positive control: gravity then lays it down by itself.
  lying.run(400);
  expectLyingFlushAtRest(lying);
}

TEST(CapsuleResting, ACapsuleLyingByItsColliderRotationRestsFlush)
{
  // The owner stands upright and the collider's own rotation lays the capsule down, so the owner's up is not its axis.
  Lying lying({ 0, 2.0f, 0 }, { 0, 0, 1.0f }, { 0, 0, 0 }, { 0, 0, 90 });
  EXPECT_GT(lying.tiltDegrees(), 0.5f);

  lying.run(400);

  expectLyingFlushAtRest(lying);
}
