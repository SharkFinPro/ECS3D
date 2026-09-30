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
    int collisionThreads = 6;
    int physicsThreads = 1;
    bool detailedTiming = false;
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
      collisions.setThreadCount(config.collisionThreads);
      collisions.setDetailedTimingEnabled(config.detailedTiming);
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
    std::vector<glm::vec3> velocities;
    std::vector<glm::vec3> spins;
    size_t totalEnters = 0;
    PhysicsSystem::IntegratePath lastPath = PhysicsSystem::IntegratePath::serial;
  };

  using Builder = std::function<void(World&)>;

  class PhysicsThreads {
  public:
    explicit PhysicsThreads(const int threads)
    {
      PhysicsSystem::setThreadCount(threads);
    }

    ~PhysicsThreads()
    {
      PhysicsSystem::setThreadCount(1);
    }

    PhysicsThreads(const PhysicsThreads&) = delete;
    PhysicsThreads& operator=(const PhysicsThreads&) = delete;
  };

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

  // Several separated clumps of mixed bodies resting on one ground, plus a few singles that fall alone.
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

  // A grid of separate bodies, none of them nested, dropped from a few heights onto one ground.
  void buildFreeBodies(World& world)
  {
    addGround(world);

    std::mt19937 random(7);
    for (int j = 0; j < 240; ++j)
    {
      const glm::vec3 position(static_cast<float>(j % 16 - 8) * 2.2f, 1.0f + static_cast<float>(j % 3) * 0.9f,
                               static_cast<float>(j / 16 - 8) * 2.2f);

      addBody(world, random, position, "Free" + std::to_string(j));
    }
  }

  // The same, plus bodies nested under other bodies, so one body's integrate reads a Transform another writes.
  void buildFreeAndNestedBodies(World& world)
  {
    buildFreeBodies(world);

    for (int i = 0; i < 3; ++i)
    {
      const float x = static_cast<float>(i) * 4.0f - 4.0f;
      const auto parent = fixtures::addObject(world.scene, "Outer" + std::to_string(i), { x, 1.5f, 30.0f },
                                              { 0.6f, 0.6f, 0.6f });
      fixtures::addBoxCollider(parent);
      fixtures::addRigidBody(parent);
      world.track(parent);

      const auto child = fixtures::addChildObject(world.scene, "Inner" + std::to_string(i), parent);
      fixtures::transformOf(child)->setPosition({ 0.0f, 1.2f, 0.0f });
      fixtures::addBoxCollider(child);
      fixtures::addRigidBody(child);
      world.track(child);
    }
  }

  // A body under a chain of empty parents deeper than an edge's inline key chain.
  void buildDeepHierarchy(World& world)
  {
    addGround(world);

    std::shared_ptr<Object> parent;
    for (int depth = 0; depth < 6; ++depth)
    {
      parent = depth == 0 ? fixtures::addObject(world.scene, "Root", { 0.0f, 3.0f, 0.0f })
                          : fixtures::addChildObject(world.scene, "Link" + std::to_string(depth), parent);
    }

    const auto leaf = fixtures::addChildObject(world.scene, "Leaf", parent);
    fixtures::transformOf(leaf)->setScale({ 0.5f, 0.5f, 0.5f });
    fixtures::addBoxCollider(leaf);
    fixtures::addRigidBody(leaf);
    world.track(leaf);

    const auto rider = fixtures::addObject(world.scene, "Rider", { 0.1f, 9.0f, 0.0f }, { 0.5f, 0.5f, 0.5f });
    fixtures::addBoxCollider(rider);
    fixtures::addRigidBody(rider);
    world.track(rider);
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
    const PhysicsThreads physicsThreads(config.physicsThreads);

    World world(config);
    build(world);

    Trace trace;

    for (size_t tick = 0; tick < ticks; ++tick)
    {
      PhysicsSystem::fixedUpdate(*world.scene.objectManager, dt);
      trace.lastPath = PhysicsSystem::lastIntegratePath();
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

      const auto body = object->getComponent<RigidBody>(ComponentType::rigidBody);
      trace.velocities.push_back(body ? body->getVelocity() : glm::vec3(0));
      trace.spins.push_back(body ? body->getAngularVelocity() : glm::vec3(0));
    }

    return trace;
  }

  void expectSameVector(const glm::vec3& expected, const glm::vec3& actual, const char* what, const size_t object)
  {
    EXPECT_EQ(expected.x, actual.x) << what << " of object " << object;
    EXPECT_EQ(expected.y, actual.y) << what << " of object " << object;
    EXPECT_EQ(expected.z, actual.z) << what << " of object " << object;
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
      expectSameVector(expected.positions[i], actual.positions[i], "position", i);
      expectSameVector(expected.rotations[i], actual.rotations[i], "rotation", i);
      expectSameVector(expected.velocities[i], actual.velocities[i], "velocity", i);
      expectSameVector(expected.spins[i], actual.spins[i], "spin", i);
    }
  }
}

TEST(PhysicsOverhead, DetailedTimingDoesNotChangeTheSimulation)
{
  Config off;
  off.refresh = true;
  off.sleeping = true;

  Config on = off;
  on.detailedTiming = true;

  const auto expected = simulate(off, 250, buildClumps);
  const auto actual = simulate(on, 250, buildClumps);

  ASSERT_GT(expected.totalEnters, 50u);

  expectSameTrace(expected, actual);
}

TEST(PhysicsOverhead, DetailedTimingIsOffUnlessAskedFor)
{
  CollisionSystem collisions;
  EXPECT_FALSE(collisions.isDetailedTimingEnabled());

  collisions.setDetailedTimingEnabled(true);
  EXPECT_TRUE(collisions.isDetailedTimingEnabled());
}

TEST(PhysicsOverhead, TheTreePathMatchesTheSweepThroughAHierarchyDeeperThanTheKeyChain)
{
  Config sweep;
  sweep.mode = BroadPhaseMode::sweep;

  const Config tree;

  const auto expected = simulate(sweep, 200, buildDeepHierarchy);
  const auto actual = simulate(tree, 200, buildDeepHierarchy);

  ASSERT_GE(expected.totalEnters, 1u);

  expectSameTrace(expected, actual);
}

TEST(PhysicsOverhead, ParallelIntegrateMatchesTheSerialOneForFreeBodies)
{
  Config serial;
  serial.physicsThreads = 1;

  Config parallel;
  parallel.physicsThreads = 8;

  const auto expected = simulate(serial, 200, buildFreeBodies);
  const auto actual = simulate(parallel, 200, buildFreeBodies);

  ASSERT_GT(expected.totalEnters, 100u);

  EXPECT_EQ(expected.lastPath, PhysicsSystem::IntegratePath::serial);
  EXPECT_EQ(actual.lastPath, PhysicsSystem::IntegratePath::parallel);

  expectSameTrace(expected, actual);
}

TEST(PhysicsOverhead, ParallelIntegrateMatchesTheSerialOneWithContactRefreshAndSleepingOn)
{
  Config serial;
  serial.refresh = true;
  serial.sleeping = true;

  Config parallel = serial;
  parallel.physicsThreads = 8;

  const auto expected = simulate(serial, 250, buildClumps);
  const auto actual = simulate(parallel, 250, buildClumps);

  ASSERT_GT(expected.totalEnters, 50u);

  EXPECT_EQ(actual.lastPath, PhysicsSystem::IntegratePath::parallel);

  expectSameTrace(expected, actual);
}

TEST(PhysicsOverhead, NestedBodiesKeepTheIntegrateSerial)
{
  Config serial;
  serial.physicsThreads = 1;

  Config parallel;
  parallel.physicsThreads = 8;

  const auto expected = simulate(serial, 200, buildFreeAndNestedBodies);
  const auto actual = simulate(parallel, 200, buildFreeAndNestedBodies);

  ASSERT_GT(expected.totalEnters, 100u);

  EXPECT_EQ(expected.lastPath, PhysicsSystem::IntegratePath::serial);
  EXPECT_EQ(actual.lastPath, PhysicsSystem::IntegratePath::serialNested);

  expectSameTrace(expected, actual);
}

TEST(PhysicsOverhead, ASceneUnderTheBodyThresholdIntegratesSerially)
{
  Config parallel;
  parallel.physicsThreads = 8;

  const auto actual = simulate(parallel, 5, buildDeepHierarchy);

  EXPECT_EQ(actual.lastPath, PhysicsSystem::IntegratePath::serial);
}

TEST(PhysicsOverhead, ClampsThePhysicsThreadCountToOne)
{
  EXPECT_EQ(PhysicsSystem::getThreadCount(), 1);

  PhysicsSystem::setThreadCount(0);
  EXPECT_EQ(PhysicsSystem::getThreadCount(), 1);

  PhysicsSystem::setThreadCount(4);
  EXPECT_EQ(PhysicsSystem::getThreadCount(), 4);

  PhysicsSystem::setThreadCount(1);
}
