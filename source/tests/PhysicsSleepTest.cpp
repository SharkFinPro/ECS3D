#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "PhysicsTestHelpers.h"
#include "TestScene.h"
#include "objects/Object.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"

#include <glm/vec3.hpp>
#include <algorithm>
#include <memory>
#include <string>

namespace {
  using physicsFixtures::addBody;
  using fixtures::addObject;
  using fixtures::transformOf;

  constexpr float tick = 1.0f / 50.0f;
  constexpr int ticksPerSecond = 50;

  struct World {
    fixtures::Scene scene = fixtures::makeScene();
    CollisionSystem collisions;
    std::shared_ptr<Object> floor;

    // The floor's top is the plane y = 0.
    World()
      : floor(addObject(scene, "Floor", { 0, -1, 0 }, { 20, 1, 20 }))
    {
      fixtures::addBoxCollider(floor);
    }

    struct Box {
      std::shared_ptr<Object> object;
      std::shared_ptr<RigidBody> body;

      [[nodiscard]] glm::vec3 position() const
      {
        return transformOf(object)->getPosition();
      }
    };

    // Two units a side, resting with its bottom on y = 0 when its center is at height 1.
    Box box(const std::string& name, const glm::vec3& position)
    {
      const auto object = addObject(scene, name, position, glm::vec3(1));
      fixtures::addBoxCollider(object);

      return { object, addBody(object, true) };
    }

    void step()
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, tick);
      collisions.fixedUpdate(*scene.objectManager, tick);
    }

    void run(const int ticks)
    {
      for (int i = 0; i < ticks; ++i)
      {
        step();
      }
    }
  };
}

TEST(PhysicsSleep, ABoxRestingOnAFloorFallsAsleepAndStopsMoving)
{
  World world;
  const auto box = world.box("Box", { 0, 1.0f, 0 });

  world.step();
  EXPECT_FALSE(box.body->isAsleep());

  world.run(3 * ticksPerSecond);
  ASSERT_TRUE(box.body->isAsleep());
  EXPECT_EQ(box.body->getVelocity(), glm::vec3(0));
  EXPECT_EQ(box.body->getAngularVelocity(), glm::vec3(0));
  EXPECT_FALSE(box.body->isFalling());

  // Asleep it is not moved at all, so its position holds exactly.
  const auto rested = box.position();
  world.run(ticksPerSecond);
  EXPECT_TRUE(box.body->isAsleep());
  EXPECT_EQ(box.position(), rested);
  EXPECT_NEAR(rested.y, 1.0f, 0.01f);
}

TEST(PhysicsSleep, ABodyThatIsNotHeldUpNeverSleeps)
{
  World world;
  const auto floating = world.box("Floating", { 0, 10.0f, 0 });
  floating.body->setDoGravity(false);

  world.run(3 * ticksPerSecond);

  EXPECT_FALSE(floating.body->isAsleep());
}

TEST(PhysicsSleep, AQueuedForceWakesASleeperAndMovesIt)
{
  World world;
  const auto box = world.box("Box", { 0, 1.0f, 0 });
  world.run(3 * ticksPerSecond);
  ASSERT_TRUE(box.body->isAsleep());
  const float startX = box.position().x;

  box.body->addPendingForce({ 0.2f, 0, 0 }, box.position(), ForceMode::velocityChange);
  EXPECT_FALSE(box.body->isAsleep());

  world.run(5);
  EXPECT_GT(box.position().x, startX + 0.1f);
}

TEST(PhysicsSleep, ASleeperMovedFromOutsideWakesAndFallsBackToTheFloor)
{
  World world;
  const auto box = world.box("Box", { 0, 1.0f, 0 });
  world.run(3 * ticksPerSecond);
  ASSERT_TRUE(box.body->isAsleep());

  transformOf(box.object)->setPosition({ 0, 3.0f, 0 });
  world.step();
  EXPECT_FALSE(box.body->isAsleep());
  EXPECT_LT(box.body->getVelocity().y, 0.0f);

  world.run(2 * ticksPerSecond);
  EXPECT_NEAR(box.position().y, 1.0f, 0.01f);
}

TEST(PhysicsSleep, ASleeperWhoseFloorMovesAwayFallsFromTheFirstTick)
{
  World world;
  const auto box = world.box("Box", { 0, 1.0f, 0 });
  world.run(3 * ticksPerSecond);
  ASSERT_TRUE(box.body->isAsleep());

  transformOf(world.floor)->setPosition({ 0, -50, 0 });
  world.step();

  // It takes the gravity it was passed by while asleep that tick, as an awake body would have.
  EXPECT_FALSE(box.body->isAsleep());
  EXPECT_LT(box.body->getVelocity().y, 0.0f);

  world.run(10);
  EXPECT_LT(box.position().y, 0.9f);
}

TEST(PhysicsSleep, ABodyLandingOnASleeperWakesItAndRestsOnIt)
{
  World world;
  const auto below = world.box("Below", { 0, 1.0f, 0 });
  world.run(3 * ticksPerSecond);
  ASSERT_TRUE(below.body->isAsleep());

  const auto above = world.box("Above", { 0, 5.0f, 0 });
  bool woke = false;
  for (int i = 0; i < 2 * ticksPerSecond; ++i)
  {
    world.step();
    woke = woke || !below.body->isAsleep();
  }

  EXPECT_TRUE(woke);
  EXPECT_NEAR(above.position().y, 3.0f, 0.02f);
  EXPECT_NEAR(below.position().y, 1.0f, 0.02f);
}

TEST(PhysicsSleep, AStackSleepsAsOneGroupAndWakesTogether)
{
  World world;
  const auto bottom = world.box("Bottom", { 0, 1.0f, 0 });
  const auto top = world.box("Top", { 0, 3.0f, 0 });
  world.run(4 * ticksPerSecond);

  ASSERT_TRUE(bottom.body->isAsleep());
  ASSERT_TRUE(top.body->isAsleep());
  EXPECT_NE(bottom.body->getSleepGroup(), 0u);
  EXPECT_EQ(bottom.body->getSleepGroup(), top.body->getSleepGroup());

  bottom.body->setVelocity({ 0.001f, 0, 0 });
  world.step();

  EXPECT_FALSE(bottom.body->isAsleep());
  EXPECT_FALSE(top.body->isAsleep());
}

TEST(PhysicsSleep, ASleepingPairKeepsReportingItsTouchAsAStay)
{
  World world;
  const auto box = world.box("Box", { 0, 1.0f, 0 });
  world.run(3 * ticksPerSecond);
  ASSERT_TRUE(box.body->isAsleep());

  const auto pair = CollisionPair::make(world.floor->getUUID(), box.object->getUUID());
  for (int i = 0; i < 10; ++i)
  {
    world.step();
    const auto& stays = world.collisions.getCollisionStays();

    EXPECT_TRUE(std::ranges::find(stays, pair) != stays.end()) << i;
    EXPECT_TRUE(world.collisions.getCollisionExits().empty()) << i;
  }
}

TEST(PhysicsSleep, ASleepersImpulsesAreKeptForWhenItWakes)
{
  World world;
  const auto box = world.box("Box", { 0, 1.0f, 0 });
  world.run(3 * ticksPerSecond);
  ASSERT_TRUE(box.body->isAsleep());
  const auto cached = world.collisions.getContactCache().size();
  ASSERT_GT(cached, 0u);

  world.run(ticksPerSecond);

  EXPECT_EQ(world.collisions.getContactCache().size(), cached);
}
