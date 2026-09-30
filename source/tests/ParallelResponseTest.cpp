#include <gtest/gtest.h>

#include "TestScene.h"
#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"

#include <glm/vec3.hpp>
#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>
#include <uuid.h>

namespace {
  constexpr float dt = 1.0f / 50.0f;

  using IndexPair = std::pair<size_t, size_t>;

  struct Config {
    BroadPhaseMode mode = BroadPhaseMode::tree;
    bool parallelResponse = false;
    int threads = 6;
    bool refresh = false;
    bool sleeping = false;
  };

  struct World {
    fixtures::Scene scene = fixtures::makeScene();
    CollisionSystem collisions;
    std::vector<std::shared_ptr<Object>> objects;
    std::map<uuids::uuid, size_t> indexOf;

    explicit World(const Config& config)
    {
      collisions.setBroadPhaseMode(config.mode);
      collisions.setThreadCount(config.threads);
      collisions.setParallelResponseEnabled(config.parallelResponse);
      collisions.setContactRefreshEnabled(config.refresh);
      collisions.setSleepingEnabled(config.sleeping);
    }

    void track(const std::shared_ptr<Object>& object)
    {
      indexOf[object->getUUID()] = objects.size();
      objects.push_back(object);
    }
  };

  struct Trace {
    std::vector<std::vector<IndexPair>> enters;
    std::vector<std::vector<IndexPair>> stays;
    std::vector<std::vector<IndexPair>> exits;
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> rotations;
    size_t totalEnters = 0;
  };

  using Builder = std::function<void(World&)>;

  void addGround(World& world)
  {
    auto ground = fixtures::addObject(world.scene, "Ground", { 0.0f, -1.0f, 0.0f }, { 60.0f, 1.0f, 60.0f });
    fixtures::addBoxCollider(ground);
    world.track(ground);
  }

  void addBody(World& world, std::mt19937& random, const glm::vec3& position, const std::string& name)
  {
    std::uniform_real_distribution<float> size(0.3f, 0.6f);
    std::uniform_real_distribution<float> angle(0.0f, 90.0f);

    const float scale = size(random);
    auto object = fixtures::addObject(world.scene, name, position, { scale, scale, scale });
    fixtures::transformOf(object)->setRotation({ angle(random), angle(random), angle(random) });

    if (random() % 3 == 0)
    {
      fixtures::addSphereCollider(object, 1.0f);
    }
    else
    {
      fixtures::addBoxCollider(object);
    }

    fixtures::addRigidBody(object);
    world.track(object);
  }

  // Several separated clumps of 10-24 mixed bodies resting on one ground, plus a few singles that fall alone.
  void buildClumps(World& world)
  {
    addGround(world);

    std::mt19937 random(31);
    std::uniform_real_distribution<float> jitter(-0.1f, 0.1f);
    int counter = 0;

    for (int clump = 0; clump < 6; ++clump)
    {
      const int count = 10 + (clump * 7) % 21;
      const float centerX = (static_cast<float>(clump) - 2.5f) * 9.0f;
      const float centerZ = clump % 2 == 0 ? -6.0f : 6.0f;

      for (int j = 0; j < count; ++j)
      {
        const glm::vec3 position(centerX + static_cast<float>(j % 3) * 1.2f - 1.2f + jitter(random),
                                 0.8f + static_cast<float>(j / 9) * 1.3f + jitter(random),
                                 centerZ + static_cast<float>((j / 3) % 3) * 1.2f - 1.2f + jitter(random));

        addBody(world, random, position, "Body" + std::to_string(counter++));
      }
    }

    for (int i = 0; i < 4; ++i)
    {
      addBody(world, random, { -30.0f + static_cast<float>(i) * 20.0f, 6.0f + static_cast<float>(i),
                               -20.0f + static_cast<float>(i) * 13.0f }, "Single" + std::to_string(i));
    }
  }

  // A rigid-body parent with no collider of its own and two child colliders, and a second body landing on both.
  void buildCompound(World& world)
  {
    addGround(world);

    for (int i = 0; i < 3; ++i)
    {
      const float x = static_cast<float>(i) * 6.0f - 6.0f;
      const auto parent = fixtures::addObject(world.scene, "Compound" + std::to_string(i), { x, 1.2f, 0.0f },
                                              { 0.5f, 0.5f, 0.5f });
      fixtures::addRigidBody(parent);
      world.track(parent);

      for (int side = 0; side < 2; ++side)
      {
        const auto child = fixtures::addChildObject(world.scene, "Part" + std::to_string(i * 2 + side), parent);
        fixtures::transformOf(child)->setPosition({ side == 0 ? -1.0f : 1.0f, 0.0f, 0.0f });

        if (i % 2 == 1)
        {
          fixtures::addSphereCollider(child, 1.0f);
        }
        else
        {
          fixtures::addBoxCollider(child);
        }

        world.track(child);
      }

      const auto rider = fixtures::addObject(world.scene, "Rider" + std::to_string(i),
                                             { x + 0.2f, 2.6f, 0.1f }, { 1.2f, 0.7f, 1.2f });
      fixtures::addBoxCollider(rider);
      fixtures::addRigidBody(rider);
      world.track(rider);
    }
  }

  // A body nested under another body, so the child's world placement follows a Transform another body's
  // response moves, with a rider landing on the child while the parent lands on the ground.
  void buildNested(World& world)
  {
    addGround(world);

    for (int i = 0; i < 3; ++i)
    {
      const float x = static_cast<float>(i) * 4.0f - 4.0f;
      const auto parent = fixtures::addObject(world.scene, "Outer" + std::to_string(i), { x, 1.5f, 4.0f },
                                              { 0.6f, 0.6f, 0.6f });
      fixtures::addBoxCollider(parent);
      fixtures::addRigidBody(parent);
      world.track(parent);

      const auto child = fixtures::addChildObject(world.scene, "Inner" + std::to_string(i), parent);
      fixtures::transformOf(child)->setPosition({ 0.0f, 1.2f, 0.0f });
      fixtures::addBoxCollider(child);
      fixtures::addRigidBody(child);
      world.track(child);

      const auto rider = fixtures::addObject(world.scene, "Rider" + std::to_string(i), { x, 5.0f, 4.0f },
                                             { 0.5f, 0.5f, 0.5f });
      fixtures::addBoxCollider(rider);
      fixtures::addRigidBody(rider);
      world.track(rider);
    }
  }

  std::vector<IndexPair> toIndexPairs(const World& world, const std::vector<CollisionPair>& pairs)
  {
    std::vector<IndexPair> result;
    for (const auto& pair : pairs)
    {
      const auto a = world.indexOf.at(pair.a);
      const auto b = world.indexOf.at(pair.b);
      result.emplace_back(std::min(a, b), std::max(a, b));
    }

    std::ranges::sort(result);

    return result;
  }

  Trace simulate(const Config& config, const size_t ticks, const Builder& build)
  {
    World world(config);
    build(world);

    Trace trace;

    for (size_t tick = 0; tick < ticks; ++tick)
    {
      PhysicsSystem::fixedUpdate(*world.scene.objectManager, dt);
      world.collisions.fixedUpdate(*world.scene.objectManager, dt);

      trace.enters.push_back(toIndexPairs(world, world.collisions.getCollisionEnters()));
      trace.stays.push_back(toIndexPairs(world, world.collisions.getCollisionStays()));
      trace.exits.push_back(toIndexPairs(world, world.collisions.getCollisionExits()));
      trace.totalEnters += trace.enters.back().size();
    }

    for (const auto& object : world.objects)
    {
      trace.positions.push_back(fixtures::positionOf(object));
      trace.rotations.push_back(fixtures::transformOf(object)->getRotation());
    }

    return trace;
  }

  void expectSameTrace(const Trace& expected, const Trace& actual)
  {
    ASSERT_EQ(expected.enters.size(), actual.enters.size());

    for (size_t tick = 0; tick < expected.enters.size(); ++tick)
    {
      EXPECT_EQ(expected.enters[tick], actual.enters[tick]) << "enters differ at tick " << tick;
      EXPECT_EQ(expected.stays[tick], actual.stays[tick]) << "stays differ at tick " << tick;
      EXPECT_EQ(expected.exits[tick], actual.exits[tick]) << "exits differ at tick " << tick;
    }

    ASSERT_EQ(expected.positions.size(), actual.positions.size());

    for (size_t i = 0; i < expected.positions.size(); ++i)
    {
      EXPECT_EQ(expected.positions[i].x, actual.positions[i].x) << "object " << i;
      EXPECT_EQ(expected.positions[i].y, actual.positions[i].y) << "object " << i;
      EXPECT_EQ(expected.positions[i].z, actual.positions[i].z) << "object " << i;
      EXPECT_EQ(expected.rotations[i].x, actual.rotations[i].x) << "object " << i;
      EXPECT_EQ(expected.rotations[i].y, actual.rotations[i].y) << "object " << i;
      EXPECT_EQ(expected.rotations[i].z, actual.rotations[i].z) << "object " << i;
    }
  }
}

TEST(ParallelResponse, MatchesTheSweepBaselineOverSeparatedClumps)
{
  Config sweep;
  sweep.mode = BroadPhaseMode::sweep;

  Config parallel;
  parallel.parallelResponse = true;
  parallel.threads = 8;

  const auto expected = simulate(sweep, 300, buildClumps);
  const auto actual = simulate(parallel, 300, buildClumps);

  // Positive control: a scene that never collides would make the comparison vacuous.
  ASSERT_GT(expected.totalEnters, 50u);

  expectSameTrace(expected, actual);
}

TEST(ParallelResponse, MatchesTheSerialResponseWithContactRefreshOn)
{
  Config serial;
  serial.refresh = true;

  Config parallel = serial;
  parallel.parallelResponse = true;
  parallel.threads = 8;

  const auto expected = simulate(serial, 250, buildClumps);
  const auto actual = simulate(parallel, 250, buildClumps);

  ASSERT_GT(expected.totalEnters, 50u);

  expectSameTrace(expected, actual);
}

TEST(ParallelResponse, MatchesTheSerialResponseWithRefreshAndSleepingOn)
{
  Config serial;
  serial.refresh = true;
  serial.sleeping = true;

  Config parallel = serial;
  parallel.parallelResponse = true;
  parallel.threads = 8;

  const auto expected = simulate(serial, 250, buildClumps);
  const auto actual = simulate(parallel, 250, buildClumps);

  ASSERT_GT(expected.totalEnters, 50u);

  expectSameTrace(expected, actual);
}

TEST(ParallelResponse, KeepsCompoundBodiesTogether)
{
  Config serial;

  Config parallel;
  parallel.parallelResponse = true;
  parallel.threads = 8;

  const auto expected = simulate(serial, 200, buildCompound);
  const auto actual = simulate(parallel, 200, buildCompound);

  ASSERT_GT(expected.totalEnters, 5u);

  expectSameTrace(expected, actual);
}

TEST(ParallelResponse, KeepsANestedBodyWithItsAncestor)
{
  Config serial;

  Config parallel;
  parallel.parallelResponse = true;
  parallel.threads = 8;

  const auto expected = simulate(serial, 200, buildNested);
  const auto actual = simulate(parallel, 200, buildNested);

  ASSERT_GT(expected.totalEnters, 5u);

  expectSameTrace(expected, actual);
}

TEST(ParallelResponse, TheThreadCountDoesNotChangeTheResult)
{
  Config one;
  one.parallelResponse = true;
  one.threads = 1;

  Config eight = one;
  eight.threads = 8;

  const auto expected = simulate(one, 250, buildClumps);
  const auto actual = simulate(eight, 250, buildClumps);

  ASSERT_GT(expected.totalEnters, 50u);

  expectSameTrace(expected, actual);
}

TEST(ParallelResponse, ClampsTheThreadCountToOne)
{
  CollisionSystem collisions;
  EXPECT_EQ(collisions.getThreadCount(), 6);

  collisions.setThreadCount(0);
  EXPECT_EQ(collisions.getThreadCount(), 1);

  collisions.setThreadCount(12);
  EXPECT_EQ(collisions.getThreadCount(), 12);
}
