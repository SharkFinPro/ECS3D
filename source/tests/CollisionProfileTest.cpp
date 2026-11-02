#include <gtest/gtest.h>

#include "TestScene.h"
#include "CollisionSystem.h"
#include "Lane.h"
#include "ProfileScope.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"

#include <glm/vec3.hpp>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace {
  using fixtures::makeScene;

  constexpr float dt = 1.0f / 50.0f;

  // Fake clock that moves on every read, so each scope has a nonzero duration.
  struct SteppingClock {
    profile::Clock::time_point operator()()
    {
      now += std::chrono::microseconds(10);
      return now;
    }

    profile::Clock::time_point now = profile::Clock::time_point{} + std::chrono::seconds(1);
  };

  uint16_t callsAtRoot(const profile::Lane& lane, const std::string_view name)
  {
    for (const auto node : lane.tree().children(0))
    {
      if (profile::nameOf(lane.tree().name(node)) == name)
      {
        return lane.history().newest().nodeCalls[node];
      }
    }

    return 0;
  }

  // A row of overlapping dynamic boxes, so the narrow phase has pairs to resolve and OpenMP gets several
  // iterations to hand out.
  fixtures::Scene makeCrowdedScene()
  {
    auto scene = makeScene();

    for (int i = 0; i < 20; ++i)
    {
      const auto object = addObject(scene, "box" + std::to_string(i), glm::vec3(0.5f * static_cast<float>(i), 0.0f, 0.0f));
      fixtures::addBoxCollider(object);
      fixtures::addRigidBody(object);
    }

    return scene;
  }
}

TEST(CollisionProfile, RecordsEachPhaseOnceFromTheCallingThread)
{
  const auto scene = makeCrowdedScene();
  CollisionSystem collisions;

  profile::Lane lane("test", 4, SteppingClock{});
  lane.bindToThisThread();
  lane.beginSample();

  collisions.fixedUpdate(*scene.objectManager, dt);

  lane.endSample(true);
  profile::Lane::unbindThisThread();

  EXPECT_FALSE(collisions.getCollisionEnters().empty());
  EXPECT_GE(callsAtRoot(lane, "Broad phase"), 1);
  EXPECT_EQ(callsAtRoot(lane, "Narrow phase"), 1);
  EXPECT_EQ(callsAtRoot(lane, "Response"), 1);
  EXPECT_EQ(lane.abandonedScopeCount(), 0u);
  EXPECT_EQ(lane.overflowCount(), 0u);
}

TEST(CollisionProfile, NarrowPhaseIsOneEntryPerUpdate)
{
  const auto scene = makeCrowdedScene();
  CollisionSystem collisions;

  profile::Lane lane("test", 4, SteppingClock{});
  lane.bindToThisThread();
  lane.beginSample();

  collisions.fixedUpdate(*scene.objectManager, dt);
  collisions.fixedUpdate(*scene.objectManager, dt);

  lane.endSample(true);
  profile::Lane::unbindThisThread();

  EXPECT_EQ(callsAtRoot(lane, "Narrow phase"), 2);
  EXPECT_EQ(callsAtRoot(lane, "Response"), 2);
}

TEST(CollisionProfile, RecordsNothingWithoutABoundLane)
{
  const auto scene = makeCrowdedScene();
  CollisionSystem collisions;

  profile::Lane lane("test", 4, SteppingClock{});
  lane.beginSample();

  collisions.fixedUpdate(*scene.objectManager, dt);

  lane.endSample(true);

  EXPECT_FALSE(collisions.getCollisionEnters().empty());
  EXPECT_EQ(callsAtRoot(lane, "Narrow phase"), 0);
}
