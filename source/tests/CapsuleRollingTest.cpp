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
#include <algorithm>
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
    bool box = false;
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
      if (Config.box)
      {
        // The capsule's footprint: two long and one wide, with the same underside height.
        transformOf(object)->setScale({ 1, 0.5f, 0.5f });
        fixtures::addBoxCollider(object);
      }
      else if (Config.capsule)
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

  // A box with the same friction, mass and speed loses nothing to tipping, so it is where the capsule should stop.
  auto boxConfig = Config;
  boxConfig.box = true;
  boxConfig.rotation = { 0, 0, 0 };
  Roller box(boxConfig);
  box.run(60);
  EXPECT_GT(box.x(), 1.0f);
  EXPECT_NEAR(stopped, box.x(), 0.01f);
  EXPECT_LT(capsule.speed(), restSpeed);
  EXPECT_EQ(capsule.spinSpeed(), 0.0f);

  // Positive control: it is still sliding at the start, so the slab and the push are not what stopped it.
  Roller early(Config);
  early.run(3);
  EXPECT_LT(early.x(), stopped);
  EXPECT_GT(early.body->getVelocity().x, 0.0f);
}

TEST(CapsuleRolling, FrictionAlongALineContactDoesNotPitchTheCapsule)
{
  Config along;
  along.rotation = { 0, 0, 90 };
  along.velocity = { 0.3f, 0, 0 };
  Roller sliding(along);

  // The capsule lies along x. Pitching it would spin it about z, which the line's far end holds against.
  float pitch = 0.0f;
  for (int tick = 0; tick < 10; ++tick)
  {
    sliding.run(1);
    pitch = std::max(pitch, std::fabs(sliding.body->getAngularVelocity().z));
  }
  EXPECT_LT(pitch, 0.5f);
  EXPECT_LT(sliding.body->getVelocity().x, 0.3f);

  // Positive control: pushed across its axis it rolls about the axis, so the contact does turn it.
  Config across;
  across.rotation = { 0, 0, 90 };
  across.velocity = { 0, 0, 0.3f };
  Roller rolling(across);

  float roll = 0.0f;
  for (int tick = 0; tick < 10; ++tick)
  {
    rolling.run(1);
    roll = std::max(roll, std::fabs(rolling.body->getAngularVelocity().x));
  }
  EXPECT_GT(roll, 5.0f);
}

TEST(CapsuleRolling, ADynamicBoxUnderACapsuleIsNotPitchedByFrictionAlongTheLine)
{
  // The box's spin about z after the capsule's first tick on it. Where the capsule's weight lands off the box's
  // center also turns it, so friction is judged against the same slide with none.
  const auto spinOfBox = [](const glm::vec3& capsuleRotation, const float friction)
  {
    const auto scene = makeScene();
    const auto support = addObject(scene, "Support", { 0, 0, 0 });
    transformOf(support)->setScale({ 4, 1, 4 });
    fixtures::addBoxCollider(support);
    const auto supportBody = addBody(support, false);

    const auto capsule = addObject(scene, "Capsule", { 0, 1.4f, 0 });
    transformOf(capsule)->setRotation(capsuleRotation);
    fixtures::addCapsuleCollider(capsule, 0.5f, 2.0f);
    const auto body = addBody(capsule, false);
    body->setVelocity({ 1.0f, -0.2f, 0 });
    body->setFriction(friction);

    CollisionSystem collisions;
    PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
    collisions.fixedUpdate(*scene.objectManager, dt);

    return supportBody->getAngularVelocity().z;
  };

  // The capsule lies along x, the way it slides, so friction must not pitch the box about z.
  EXPECT_NEAR(spinOfBox({ 0, 0, 90 }, 0.5f), spinOfBox({ 0, 0, 90 }, 0.0f), 0.5f);

  // Positive control: lying along z, the same slide is across the line, and friction does turn the box.
  EXPECT_GT(std::fabs(spinOfBox({ 90, 0, 0 }, 0.5f) - spinOfBox({ 90, 0, 0 }, 0.0f)), 1.0f);
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
