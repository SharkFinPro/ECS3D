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
#include <cmath>
#include <glm/vec3.hpp>
#include <memory>
#include <utility>

namespace {
  using namespace physicsFixtures;
  using fixtures::addObject;
  using fixtures::makeScene;
  using fixtures::transformOf;

  // Gravity is added each tick and the contact takes it out again, which leaves a little float noise in the
  // velocity of a body at rest; comeToRest drops the spin exactly.
  constexpr float restSpeed = 1e-5f;

  struct Config {
    bool capsule = true;
    float height = 2.0f;
    glm::vec3 position{ 0, 1.6f, 0 };
    glm::vec3 rotation{ 0 };
    glm::vec3 velocity{ 0 };
    glm::vec3 spin{ 0 };
    float friction = -1.0f;
    float slab = 20.0f;
  };

  struct Roller {
    fixtures::Scene scene = makeScene();
    std::shared_ptr<Object> object;
    std::shared_ptr<RigidBody> body;
    CollisionSystem collisions;

    explicit Roller(const Config& Config)
    {
      // A slab under the roller, wide enough that nothing rolls off it; its top face is y = 1.
      const auto ground = addObject(scene, "Ground", { Config.position.x, 0, 0 });
      transformOf(ground)->setScale({ Config.slab, 1, Config.slab });
      fixtures::addBoxCollider(ground);

      object = addObject(scene, "Roller", Config.position);
      transformOf(object)->setRotation(Config.rotation);
      if (Config.capsule)
      {
        fixtures::addCapsuleCollider(object, 0.5f, Config.height);
      }
      else
      {
        fixtures::addSphereCollider(object, 0.5f);
      }

      body = addBody(object, true);
      body->setVelocity(Config.velocity);
      body->setAngularVelocity(Config.spin);
      if (Config.friction >= 0.0f)
      {
        body->setFriction(Config.friction);
      }
    }

    void run(const int ticks, const float step = dt)
    {
      for (int tick = 0; tick < ticks; ++tick)
      {
        PhysicsSystem::fixedUpdate(*scene.objectManager, step);
        collisions.fixedUpdate(*scene.objectManager, step);
      }
    }

    [[nodiscard]] float x() const
    {
      return transformOf(object)->getPosition().x;
    }

    [[nodiscard]] float speed() const
    {
      return glm::length(body->getVelocity());
    }

    [[nodiscard]] float spinSpeed() const
    {
      return glm::length(body->getAngularVelocity());
    }
  };

  Config pushed(const bool capsule, const float slab = 20.0f)
  {
    Config Config;
    Config.capsule = capsule;
    Config.rotation = capsule ? glm::vec3(90, 0, 0) : glm::vec3(0);
    Config.velocity = { 0.3f, 0, 0 };
    Config.slab = slab;
    return Config;
  }

  // Stops within a fifth of the sphere's stop position on the same slab.
  void expectStopsLikeSphere(const Roller& capsule, const Roller& sphere)
  {
    EXPECT_GT(sphere.x(), 1.0f);
    EXPECT_NEAR(capsule.x(), sphere.x(), 0.2f * sphere.x());
  }
}

TEST(CapsuleRolling, ACapsuleLyingAcrossItsPushStopsNearWhereASphereDoes)
{
  Roller sphere(pushed(false));
  Roller capsule(pushed(true));

  sphere.run(300);
  capsule.run(300);

  expectStopsLikeSphere(capsule, sphere);
}

TEST(CapsuleRolling, WithoutFrictionTheSameCapsuleRollsFarPastWhereItStops)
{
  auto Config = pushed(true, 200.0f);
  Roller resisted(Config);
  Config.friction = 0.0f;
  Roller free(Config);

  resisted.run(100);
  free.run(100);

  EXPECT_GT(free.x(), 3.0f * resisted.x());
}

TEST(CapsuleRolling, ACapsuleRollingAcrossItsAxisComesToExactRest)
{
  Roller capsule(pushed(true));

  capsule.run(300);

  EXPECT_LT(capsule.speed(), restSpeed);
  EXPECT_EQ(capsule.spinSpeed(), 0.0f);
}

TEST(CapsuleRolling, ACapsuleRollingAtTheServerTickStopsNearWhereASphereDoes)
{
  constexpr float serverTick = 0.02f;
  Roller sphere(pushed(false, 60.0f));
  Roller capsule(pushed(true, 60.0f));

  sphere.run(1500, serverTick);
  capsule.run(1500, serverTick);

  // Well inside the slab, so the stop is the resistance and not the edge.
  EXPECT_LT(sphere.x(), 30.0f);
  expectStopsLikeSphere(capsule, sphere);
  EXPECT_LT(capsule.speed(), restSpeed);
  EXPECT_EQ(capsule.spinSpeed(), 0.0f);
}

TEST(CapsuleRolling, ASpinningCapsuleDroppedOnTheBoxComesToExactRest)
{
  Config Config;
  Config.position = { 0, 4, 0 };
  Config.rotation = { 10, 20, 60 };
  Config.velocity = { 0.1f, 0, 0 };
  Config.spin = { 30, 60, 0 };
  Roller capsule(Config);

  capsule.run(400);

  EXPECT_LT(capsule.speed(), restSpeed);
  EXPECT_EQ(capsule.spinSpeed(), 0.0f);
}

TEST(CapsuleRolling, ALyingCapsuleSpinningAboutTheNormalGrindsToExactRest)
{
  // The spin is about the vertical through the center of mass, so only the line contact's twist resistance can
  // stop it. The first case has a core as long as the capsule, the second a short one, the third sits far from
  // the origin where float noise is larger.
  for (const auto& [height, x] : { std::pair{ 2.0f, 0.0f }, std::pair{ 1.2f, 0.0f }, std::pair{ 2.0f, 1000.0f } })
  {
    SCOPED_TRACE(::testing::Message() << "height " << height << " x " << x);

    Config Config;
    Config.height = height;
    Config.position = { x, 1.5f, 0 };
    Config.rotation = { 0, 0, 90 };
    Config.spin = { 0, 100, 0 };
    Roller grinding(Config);
    Config.friction = 0.0f;
    Roller frictionless(Config);

    grinding.run(300);
    frictionless.run(300);

    EXPECT_EQ(grinding.spinSpeed(), 0.0f);
    // Positive control: only the damping slows it without friction, so the stop is the grinding.
    EXPECT_GT(frictionless.spinSpeed(), 1.0f);
  }
}

TEST(CapsuleRolling, ACapsuleSlidingAlongItsAxisStopsWithoutRolling)
{
  Config Config;
  Config.rotation = { 0, 0, 90 };
  Config.velocity = { 0.3f, 0, 0 };
  Roller capsule(Config);

  capsule.run(60);
  const float stopped = capsule.x();

  EXPECT_NEAR(stopped, 1.19f, 0.05f);
  EXPECT_LT(capsule.speed(), restSpeed);
  EXPECT_EQ(capsule.spinSpeed(), 0.0f);

  // Positive control: it is still sliding at the start, so the slab and the push are not what stopped it.
  Roller early(Config);
  early.run(3);
  EXPECT_LT(early.x(), stopped);
  EXPECT_GT(early.body->getVelocity().x, 0.0f);
}

TEST(CapsuleRolling, ACapsuleTumblingOnItsCapRollsAboutAnyAxisAndComesToRest)
{
  Config config;
  config.position = { 0, 4, 0 };
  config.rotation = { 0, 0, 10 };
  config.velocity = { 0.2f, 0, 0.1f };
  config.spin = { 40, 0, 40 };
  Roller capsule(config);

  capsule.run(500);

  EXPECT_LT(capsule.speed(), restSpeed);
  EXPECT_EQ(capsule.spinSpeed(), 0.0f);
  EXPECT_LT(std::fabs(capsule.x()), 15.0f);
}
