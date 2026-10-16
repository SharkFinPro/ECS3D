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
#include <memory>
#include <utility>

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
  // Each rotation lays the capsule along the axis it slides on.
  const std::pair<glm::vec3, glm::vec3> slides[] = { { { 0, 0, 90 }, { 0.3f, 0, 0 } },
                                                      { { 90, 0, 0 }, { 0, 0, 0.3f } } };
  for (const auto& [rotation, velocity] : slides)
  {
    SCOPED_TRACE(::testing::Message() << "rotation " << rotation.x << " " << rotation.z);

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

TEST(CapsuleResting, ACapsuleJustPastTheFlushLimitIsNotSnappedOnContact)
{
  // Both start a hair above the slab, so they touch on the first tick. The first capsule's end gap (sin 1.5
  // degrees = 0.026) is outside the manifold's line band, so it has one contact point; the control is inside it.
  Lying past({ 0, 1.505f, 0 }, { 0, 0, 91.5f }, { 0, 0, 0 });
  Lying inside({ 0, 1.505f, 0 }, { 0, 0, 90.5f }, { 0, 0, 0 });
  EXPECT_NEAR(past.tiltDegrees(), 1.5f, 1e-3f);

  past.run(1);
  inside.run(1);

  // Both touched the slab on that tick: free fall would have lowered them, and the push out raised them.
  EXPECT_GT(transformOf(past.object)->getPosition().y, 1.505f);
  EXPECT_GT(transformOf(inside.object)->getPosition().y, 1.5f);
  // Positive control: the one inside the limit was laid flush on that tick, the one past it was left tilted.
  EXPECT_LT(inside.tiltDegrees(), flushToleranceDegrees);
  EXPECT_GT(past.tiltDegrees(), 1.4f);

  // It then lays itself down by gravity.
  past.run(400);
  expectLyingFlushAtRest(past);
}

TEST(CapsuleResting, ACapsuleInTheLineBandPastADegreeIsLaidFlush)
{
  // sin 1.1 degrees = 0.0192: past a degree, inside the manifold's 0.021 line band, so both ends touch.
  Lying lying({ 0, 1.505f, 0 }, { 0, 0, 91.1f }, { 0, 0, 0 });
  EXPECT_NEAR(lying.tiltDegrees(), 1.1f, 1e-3f);

  lying.run(1);
  EXPECT_LT(lying.tiltDegrees(), flushToleranceDegrees);

  lying.run(400);
  expectLyingFlushAtRest(lying);
}

TEST(CapsuleResting, ACapsuleLyingByItsColliderRotationRestsFlush)
{
  // The owner stands upright and the collider's own rotation lays the capsule down, so the owner's up is not its axis.
  Lying lying({ 0, 2.0f, 0 }, { 0, 0, 0.5f }, { 0, 0, 0 }, { 0, 0, 90 });
  EXPECT_NEAR(lying.tiltDegrees(), 0.5f, 1e-3f);

  lying.run(400);

  expectLyingFlushAtRest(lying);
}
