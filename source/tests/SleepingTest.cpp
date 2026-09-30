#include <gtest/gtest.h>

#include "TestScene.h"
#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"

#include <glm/vec3.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {
  constexpr float dt = 1.0f / 50.0f;
  constexpr int settleTicks = 400;

  struct Sim {
    fixtures::Scene scene = fixtures::makeScene();
    CollisionSystem collisions;
    std::shared_ptr<Object> ground;
    std::vector<std::shared_ptr<Object>> boxes;

    explicit Sim(const bool sleepingEnabled = true)
    {
      collisions.setBroadPhaseMode(BroadPhaseMode::tree);
      collisions.setSleepingEnabled(sleepingEnabled);

      ground = fixtures::addObject(scene, "Ground", { 0.0f, -1.0f, 0.0f }, { 20.0f, 1.0f, 20.0f });
      fixtures::addBoxCollider(ground);
    }

    std::shared_ptr<Object> addBox(const glm::vec3& position)
    {
      auto box = fixtures::addObject(scene, "Box" + std::to_string(boxes.size()), position, { 0.5f, 0.5f, 0.5f });
      fixtures::addBoxCollider(box);
      fixtures::addRigidBody(box);
      boxes.push_back(box);

      return box;
    }

    void step()
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
      collisions.fixedUpdate(*scene.objectManager, dt);
    }

    // Steps until the predicate holds, at most maxTicks; returns whether it did.
    bool stepUntil(const std::function<bool()>& done, const int maxTicks)
    {
      for (int tick = 0; tick < maxTicks; ++tick)
      {
        step();

        if (done())
        {
          return true;
        }
      }

      return false;
    }

    void stepFor(const int ticks)
    {
      for (int tick = 0; tick < ticks; ++tick)
      {
        step();
      }
    }
  };

  std::shared_ptr<RigidBody> bodyOf(const std::shared_ptr<Object>& object)
  {
    return object->getComponent<RigidBody>(ComponentType::rigidBody);
  }

  bool asleep(const std::shared_ptr<Object>& object)
  {
    return bodyOf(object)->isAsleep();
  }

  bool allAsleep(const Sim& sim)
  {
    return std::ranges::all_of(sim.boxes, [](const auto& box) { return asleep(box); });
  }

  bool anyAsleep(const Sim& sim)
  {
    return std::ranges::any_of(sim.boxes, [](const auto& box) { return asleep(box); });
  }

  void buildStack(Sim& sim, const int count)
  {
    for (int i = 0; i < count; ++i)
    {
      sim.addBox({ 0.0f, 0.52f + static_cast<float>(i) * 1.02f, 0.0f });
    }
  }

  bool contains(const std::vector<CollisionPair>& pairs, const std::shared_ptr<Object>& a,
                const std::shared_ptr<Object>& b)
  {
    return std::ranges::find(pairs, CollisionPair::make(a->getUUID(), b->getUUID())) != pairs.end();
  }
}

TEST(Sleeping, ABoxDroppedOnTheGroundFallsAsleepAndStopsMoving)
{
  Sim sim;
  const auto box = sim.addBox({ 0.0f, 0.75f, 0.0f });

  ASSERT_TRUE(sim.stepUntil([&] { return asleep(box); }, 200));

  const auto position = fixtures::positionOf(box);
  const auto updateId = fixtures::transformOf(box)->getUpdateID();

  for (int tick = 0; tick < 50; ++tick)
  {
    sim.step();

    EXPECT_TRUE(asleep(box)) << "tick " << tick;
    EXPECT_EQ(fixtures::transformOf(box)->getUpdateID(), updateId) << "tick " << tick;
  }

  fixtures::expectNear(fixtures::positionOf(box), position, 0.0f);
}

TEST(Sleeping, AStackOfThreeBoxesSleepsAsOneIsland)
{
  Sim sim;
  buildStack(sim, 3);

  ASSERT_TRUE(sim.stepUntil([&] { return allAsleep(sim); }, settleTicks));

  const auto island = bodyOf(sim.boxes[0])->getIslandId();
  EXPECT_NE(island, 0u);
  EXPECT_EQ(bodyOf(sim.boxes[1])->getIslandId(), island);
  EXPECT_EQ(bodyOf(sim.boxes[2])->getIslandId(), island);
}

TEST(Sleeping, ABoxDroppedOnASleepingStackWakesTheWholeStack)
{
  Sim sim;
  buildStack(sim, 3);

  ASSERT_TRUE(sim.stepUntil([&] { return allAsleep(sim); }, settleTicks));

  const auto stackTop = fixtures::positionOf(sim.boxes[2]).y + 0.5f;
  const auto dropped = sim.addBox({ 0.0f, stackTop + 1.5f, 0.0f });

  bool woke = false;
  for (int tick = 0; tick < 200 && !woke; ++tick)
  {
    sim.step();

    if (!asleep(sim.boxes[0]) || !asleep(sim.boxes[1]) || !asleep(sim.boxes[2]))
    {
      woke = true;

      EXPECT_FALSE(asleep(sim.boxes[0]));
      EXPECT_FALSE(asleep(sim.boxes[1]));
      EXPECT_FALSE(asleep(sim.boxes[2]));
    }
  }

  ASSERT_TRUE(woke);
  EXPECT_FALSE(asleep(dropped));

  EXPECT_TRUE(sim.stepUntil([&] { return allAsleep(sim); }, settleTicks));
}

TEST(Sleeping, APendingForceWakesABodyAndItsIsland)
{
  Sim sim;
  buildStack(sim, 2);

  ASSERT_TRUE(sim.stepUntil([&] { return allAsleep(sim); }, settleTicks));

  const auto bottom = bodyOf(sim.boxes[0]);
  bottom->addPendingForce({ 0.05f, 0.0f, 0.0f }, fixtures::positionOf(sim.boxes[0]), ForceMode::velocityChange);

  sim.step();

  EXPECT_FALSE(asleep(sim.boxes[0]));
  EXPECT_FALSE(asleep(sim.boxes[1]));
  EXPECT_TRUE(bottom->getPendingForces().empty());
}

TEST(Sleeping, MovingTheGroundWakesTheBodyRestingOnIt)
{
  Sim sim;
  const auto box = sim.addBox({ 0.0f, 0.75f, 0.0f });

  ASSERT_TRUE(sim.stepUntil([&] { return asleep(box); }, 200));

  const auto restingHeight = fixtures::positionOf(box).y;

  fixtures::transformOf(sim.ground)->setPosition(fixtures::positionOf(sim.ground) + glm::vec3(0.0f, -0.5f, 0.0f));
  sim.step();

  EXPECT_FALSE(asleep(box));

  sim.stepFor(100);

  EXPECT_LT(fixtures::positionOf(box).y, restingHeight - 0.3f);
}

TEST(Sleeping, RemovingTheGroundWakesTheBodyRestingOnIt)
{
  Sim sim;
  const auto box = sim.addBox({ 0.0f, 0.75f, 0.0f });

  ASSERT_TRUE(sim.stepUntil([&] { return asleep(box); }, 200));

  const auto restingHeight = fixtures::positionOf(box).y;

  sim.scene.objectManager->removeObject(sim.ground);
  sim.scene.objectManager->deleteObjectsMarkedForDeletion();
  sim.step();

  EXPECT_FALSE(asleep(box));

  sim.stepFor(100);

  EXPECT_LT(fixtures::positionOf(box).y, restingHeight - 1.0f);
}

TEST(Sleeping, ASleepingBodyKeepsProducingStayEventsAndNoExits)
{
  Sim sim;
  const auto box = sim.addBox({ 0.0f, 0.75f, 0.0f });

  ASSERT_TRUE(sim.stepUntil([&] { return asleep(box); }, 200));

  for (int tick = 0; tick < 30; ++tick)
  {
    sim.step();

    EXPECT_TRUE(asleep(box)) << "tick " << tick;
    EXPECT_TRUE(contains(sim.collisions.getCollisionStays(), box, sim.ground)) << "tick " << tick;
    EXPECT_TRUE(sim.collisions.getCollisionExits().empty()) << "tick " << tick;
  }
}

TEST(Sleeping, NothingSleepsWhileSleepingIsDisabled)
{
  Sim sim(false);
  const auto box = sim.addBox({ 0.0f, 0.75f, 0.0f });

  for (int tick = 0; tick < 300; ++tick)
  {
    sim.step();

    ASSERT_FALSE(asleep(box)) << "tick " << tick;
  }

  EXPECT_FALSE(anyAsleep(sim));
}
