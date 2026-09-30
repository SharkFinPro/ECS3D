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

  struct World {
    fixtures::Scene scene = fixtures::makeScene();
    CollisionSystem collisions;
    std::vector<std::shared_ptr<Object>> objects;
    std::map<uuids::uuid, size_t> indexOf;

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

  void addDynamicBody(World& world, std::mt19937& random, const glm::vec3& position, const std::string& name)
  {
    std::uniform_real_distribution<float> size(0.25f, 0.9f);
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

  std::unique_ptr<World> buildWorld(const BroadPhaseMode mode)
  {
    auto world = std::make_unique<World>();
    world->collisions.setBroadPhaseMode(mode);

    std::mt19937 random(2024);

    auto ground = fixtures::addObject(world->scene, "Ground", { 0.0f, -9.0f, 0.0f }, { 100.0f, 10.0f, 100.0f });
    fixtures::addBoxCollider(ground);
    world->track(ground);

    std::uniform_real_distribution<float> jitter(-0.2f, 0.2f);
    int counter = 0;
    for (int x = 0; x < 5; ++x)
    {
      for (int y = 0; y < 5; ++y)
      {
        for (int z = 0; z < 5; ++z)
        {
          const glm::vec3 position(static_cast<float>(x - 2) * 1.6f + jitter(random),
                                   3.0f + static_cast<float>(y) * 1.6f + jitter(random),
                                   static_cast<float>(z - 2) * 1.6f + jitter(random));

          addDynamicBody(*world, random, position, "Body" + std::to_string(counter++));
        }
      }
    }

    return world;
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

  void churn(World& world, const size_t tick)
  {
    if (tick == 60)
    {
      for (const size_t index : { 5u, 17u, 40u, 41u })
      {
        world.scene.objectManager->removeObject(world.objects[index]);
      }

      world.scene.objectManager->deleteObjectsMarkedForDeletion();
    }

    if (tick == 80)
    {
      std::mt19937 random(99);
      for (int i = 0; i < 4; ++i)
      {
        addDynamicBody(world, random, { static_cast<float>(i) * 1.3f - 2.0f, 9.0f, 0.5f }, "Late" + std::to_string(i));
      }
    }
  }

  Trace simulate(const BroadPhaseMode mode, const bool withChurn, const size_t ticks)
  {
    auto world = buildWorld(mode);
    Trace trace;

    for (size_t tick = 0; tick < ticks; ++tick)
    {
      if (withChurn)
      {
        churn(*world, tick);
      }

      PhysicsSystem::fixedUpdate(*world->scene.objectManager, dt);
      world->collisions.fixedUpdate(*world->scene.objectManager, dt);

      trace.enters.push_back(toIndexPairs(*world, world->collisions.getCollisionEnters()));
      trace.stays.push_back(toIndexPairs(*world, world->collisions.getCollisionStays()));
      trace.exits.push_back(toIndexPairs(*world, world->collisions.getCollisionExits()));
      trace.totalEnters += trace.enters.back().size();
    }

    for (const auto& object : world->objects)
    {
      trace.positions.push_back(fixtures::positionOf(object));
      trace.rotations.push_back(fixtures::transformOf(object)->getRotation());
    }

    return trace;
  }

  void expectSameTrace(const Trace& sweep, const Trace& tree)
  {
    ASSERT_EQ(sweep.enters.size(), tree.enters.size());

    for (size_t tick = 0; tick < sweep.enters.size(); ++tick)
    {
      EXPECT_EQ(sweep.enters[tick], tree.enters[tick]) << "enters differ at tick " << tick;
      EXPECT_EQ(sweep.stays[tick], tree.stays[tick]) << "stays differ at tick " << tick;
      EXPECT_EQ(sweep.exits[tick], tree.exits[tick]) << "exits differ at tick " << tick;
    }

    ASSERT_EQ(sweep.positions.size(), tree.positions.size());

    for (size_t i = 0; i < sweep.positions.size(); ++i)
    {
      EXPECT_EQ(sweep.positions[i].x, tree.positions[i].x) << "object " << i;
      EXPECT_EQ(sweep.positions[i].y, tree.positions[i].y) << "object " << i;
      EXPECT_EQ(sweep.positions[i].z, tree.positions[i].z) << "object " << i;
      EXPECT_EQ(sweep.rotations[i].x, tree.rotations[i].x) << "object " << i;
      EXPECT_EQ(sweep.rotations[i].y, tree.rotations[i].y) << "object " << i;
      EXPECT_EQ(sweep.rotations[i].z, tree.rotations[i].z) << "object " << i;
    }
  }
}

TEST(BroadPhaseEquivalence, TheTreeReproducesTheSweepThroughAFallingPile)
{
  const auto sweep = simulate(BroadPhaseMode::sweep, false, 300);
  const auto tree = simulate(BroadPhaseMode::tree, false, 300);

  // Positive control: a scene that never collides would make the comparison vacuous.
  ASSERT_GT(sweep.totalEnters, 50u);

  expectSameTrace(sweep, tree);
}

TEST(BroadPhaseEquivalence, TheTreeReproducesTheSweepWhileObjectsAreRemovedAndAdded)
{
  const auto sweep = simulate(BroadPhaseMode::sweep, true, 200);
  const auto tree = simulate(BroadPhaseMode::tree, true, 200);

  ASSERT_GT(sweep.totalEnters, 50u);

  expectSameTrace(sweep, tree);
}
