#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "PhysicsTestHelpers.h"
#include "TestScene.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <memory>

namespace {
  using namespace physicsFixtures;
  using fixtures::addObject;
  using fixtures::makeScene;
  using fixtures::transformOf;

  // Gravity is added each tick and the contact takes it out again, which leaves a little float noise in the
  // velocity of a body at rest; the spin is dropped exactly by comeToRest.
  constexpr float restSpeed = 1e-5f;

  struct Roller {
    fixtures::Scene scene = makeScene();
    std::shared_ptr<Object> object;
    std::shared_ptr<RigidBody> body;
    CollisionSystem collisions;

    Roller(const bool capsule, const glm::vec3& position, const glm::vec3& rotation, const glm::vec3& velocity,
           const glm::vec3& spin)
    {
      // A slab wide enough that nothing rolls off it; its top face is y = 1.
      const auto ground = addObject(scene, "Ground", { 0, 0, 0 });
      transformOf(ground)->setScale({ 20, 1, 20 });
      fixtures::addBoxCollider(ground);

      object = addObject(scene, "Roller", position);
      transformOf(object)->setRotation(rotation);
      if (capsule)
      {
        fixtures::addCapsuleCollider(object, 0.5f, 2.0f);
      }
      else
      {
        fixtures::addSphereCollider(object, 0.5f);
      }

      body = addBody(object, true);
      body->setVelocity(velocity);
      body->setAngularVelocity(spin);
    }

    void run(const int ticks, const float step = dt)
    {
      for (int tick = 0; tick < ticks; ++tick)
      {
        PhysicsSystem::fixedUpdate(*scene.objectManager, step);
        collisions.fixedUpdate(*scene.objectManager, step);
      }
    }

    [[nodiscard]] glm::vec3 position() const
    {
      return transformOf(object)->getPosition();
    }
  };
}

TEST(CapsuleRolling, ACapsuleLyingAcrossItsPushStopsNearWhereASphereDoes)
{
  Roller sphere(false, { 0, 1.6f, 0 }, { 0, 0, 0 }, { 0.3f, 0, 0 }, { 0, 0, 0 });
  Roller capsule(true, { 0, 1.6f, 0 }, { 90, 0, 0 }, { 0.3f, 0, 0 }, { 0, 0, 0 });

  sphere.run(300);
  capsule.run(300);

  // Positive control: the sphere rolls a few widths and stops, so the slab is long enough to show a capsule that
  // does not.
  EXPECT_GT(sphere.position().x, 3.0f);
  EXPECT_LT(sphere.position().x, 4.0f);

  EXPECT_GT(capsule.position().x, 2.0f);
  EXPECT_LT(capsule.position().x, 1.5f * sphere.position().x);
}

TEST(CapsuleRolling, ACapsuleRollingAcrossItsAxisComesToExactRest)
{
  Roller capsule(true, { 0, 1.6f, 0 }, { 90, 0, 0 }, { 0.3f, 0, 0 }, { 0, 0, 0 });

  capsule.run(300);

  EXPECT_LT(glm::length(capsule.body->getVelocity()), restSpeed);
  EXPECT_EQ(glm::length(capsule.body->getAngularVelocity()), 0.0f);
}

TEST(CapsuleRolling, ACapsuleRollingAtTheServerTickStopsNearWhereASphereDoes)
{
  constexpr float serverTick = 0.02f;
  Roller sphere(false, { 0, 1.6f, 0 }, { 0, 0, 0 }, { 0.3f, 0, 0 }, { 0, 0, 0 });
  Roller capsule(true, { 0, 1.6f, 0 }, { 90, 0, 0 }, { 0.3f, 0, 0 }, { 0, 0, 0 });

  sphere.run(1500, serverTick);
  capsule.run(1500, serverTick);

  EXPECT_GT(sphere.position().x, 1.0f);
  EXPECT_LT(capsule.position().x, 1.5f * sphere.position().x);
  EXPECT_LT(glm::length(capsule.body->getVelocity()), restSpeed);
  EXPECT_EQ(glm::length(capsule.body->getAngularVelocity()), 0.0f);
}

TEST(CapsuleRolling, ASpinningCapsuleDroppedOnTheBoxComesToExactRest)
{
  Roller capsule(true, { 0, 4, 0 }, { 10, 20, 60 }, { 0.1f, 0, 0 }, { 30, 60, 0 });

  capsule.run(400);

  EXPECT_LT(glm::length(capsule.body->getVelocity()), restSpeed);
  EXPECT_EQ(glm::length(capsule.body->getAngularVelocity()), 0.0f);
}

TEST(CapsuleRolling, ACapsuleSlidingAlongItsAxisStopsWithoutRolling)
{
  Roller capsule(true, { 0, 1.6f, 0 }, { 0, 0, 90 }, { 0.3f, 0, 0 }, { 0, 0, 0 });

  capsule.run(60);
  const float stopped = capsule.position().x;

  EXPECT_NEAR(stopped, 1.19f, 0.05f);
  EXPECT_LT(glm::length(capsule.body->getVelocity()), restSpeed);
  EXPECT_NEAR(glm::length(capsule.body->getAngularVelocity()), 0.0f, 1e-3f);

  // Positive control: it is still sliding at the start, so the slab and the push are not what stopped it.
  Roller early(true, { 0, 1.6f, 0 }, { 0, 0, 90 }, { 0.3f, 0, 0 }, { 0, 0, 0 });
  early.run(3);
  EXPECT_LT(early.position().x, stopped);
  EXPECT_GT(early.body->getVelocity().x, 0.0f);
}
