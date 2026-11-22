#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "DefaultProject.h"
#include "DefaultProjectParts.h"
#include "DefaultTerrainTiles.h"
#include "PhysicsSystem.h"
#include "ProjectSerializer.h"
#include "TestScene.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/Terrain.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"
#include "terrain/TerrainVolume.h"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <uuid.h>
#include <vector>

namespace {
  constexpr float tick = 1.0f / 50.0f;
  constexpr int ticksPerSecond = 50;
  constexpr int seconds = 20;
  constexpr int settledSeconds = 5;

  struct Pose {
    glm::vec3 position;
    glm::quat orientation;

    bool operator==(const Pose&) const = default;
  };

  // The Terrain Basin scene of the built-in project, loaded with its nine tiles generated and bound.
  struct Basin {
    std::shared_ptr<ComponentRegistry> componentRegistry = std::make_shared<ComponentRegistry>();
    AssetRegistry assetRegistry;
    SceneManager sceneManager;
    std::shared_ptr<SceneAsset> scene;
    std::array<std::shared_ptr<terrain::TerrainVolume>, basin::tileCount> tiles;
    int boundTiles = 0;
    std::vector<std::shared_ptr<Transform>> transforms;
    std::vector<std::shared_ptr<RigidBody>> bodies;

    Basin()
    {
      registerDataComponents(*componentRegistry);
      const ProjectSerializer serializer(&assetRegistry, &sceneManager, componentRegistry);
      serializer.deserialize(buildDefaultProject());

      for (const auto& [uuid, candidate] : sceneManager.getScenes())
      {
        if (candidate->getName() == "Terrain Basin")
        {
          scene = candidate;
        }
      }

      if (scene)
      {
        sceneManager.loadScene(scene);
        generateTiles();
        collectObjects();
      }
    }

    [[nodiscard]] const ObjectManager& objects() const
    {
      return *scene->getObjectManager();
    }

    [[nodiscard]] std::vector<Pose> poses() const
    {
      std::vector<Pose> result;
      result.reserve(transforms.size());
      for (const auto& transform : transforms)
      {
        result.push_back({ transform->getPosition(), transform->getOrientation() });
      }

      return result;
    }

  private:
    void generateTiles()
    {
      std::array<terrain::TerrainVolume*, basin::tileCount> pointers{};
      for (size_t i = 0; i < tiles.size(); ++i)
      {
        tiles[i] = std::make_shared<terrain::TerrainVolume>(basin::voxelSize);
        pointers[i] = tiles[i].get();
      }

      generateBasinTiles(pointers);
    }

    void bindTile(Terrain& terrain)
    {
      const auto found = std::ranges::find(defaultproject::basinTerrains, uuids::to_string(terrain.getTerrainUUID()));
      if (found != defaultproject::basinTerrains.end())
      {
        terrain.setVolume(tiles[static_cast<size_t>(found - defaultproject::basinTerrains.begin())]);
        ++boundTiles;
      }
    }

    void collectObjects()
    {
      for (const auto& object : objects().getAllObjects())
      {
        if (const auto collider = object->getComponent<Collider>(ComponentType::collider);
            collider && collider->getColliderType() == ColliderType::terrainCollider)
        {
          bindTile(static_cast<Terrain&>(*collider));
        }

        const auto body = object->getComponent<RigidBody>(ComponentType::rigidBody);
        if (body && body->getOwner() == object.get())
        {
          transforms.push_back(object->getComponent<Transform>(ComponentType::transform));
          bodies.push_back(body);
        }
      }
    }
  };

  struct SecondTiming {
    double milliseconds = 0.0;
    double worstTick = 0.0;
    size_t moved = 0;
  };

  // A second of the server's ticks: the time they took, the slowest of them, and how many bodies moved in each.
  SecondTiming runSecond(const Basin& world, CollisionSystem& collisionSystem, std::vector<Pose>& before)
  {
    SecondTiming timing;
    for (int i = 0; i < ticksPerSecond; ++i)
    {
      const auto start = std::chrono::steady_clock::now();
      PhysicsSystem::fixedUpdate(world.objects(), tick);
      collisionSystem.fixedUpdate(world.objects(), tick);
      const std::chrono::duration<double, std::milli> spent = std::chrono::steady_clock::now() - start;
      timing.milliseconds += spent.count();
      timing.worstTick = std::max(timing.worstTick, spent.count());

      const auto after = world.poses();
      for (size_t b = 0; b < after.size(); ++b)
      {
        timing.moved += after[b] == before[b] ? 0 : 1;
      }
      before = after;
    }

    return timing;
  }

  // Why a body is awake: the first sleep test it fails, or that it rests in an island one of whose bodies does not.
  const char* awakeReason(const RigidBody& body)
  {
    if (body.getNextFalling())
    {
      return "unsupported";
    }

    if (glm::length(body.getVelocity()) >= CollisionSystem::sleepSpeed * tick)
    {
      return "moving";
    }

    if (!body.isSteady())
    {
      return "on an edge or corner";
    }

    if (glm::length(body.getAngularVelocity()) >= CollisionSystem::sleepAngularSpeed)
    {
      return "turning";
    }

    return "resting in an island that is not";
  }

  void reportAwake(const std::vector<std::shared_ptr<RigidBody>>& bodies)
  {
    size_t asleep = 0;
    std::map<std::string, size_t> awake;
    for (const auto& body : bodies)
    {
      if (body->isAsleep())
      {
        ++asleep;
      }
      else
      {
        ++awake[awakeReason(*body)];
      }
    }

    std::cout << "[basin] at the end: " << asleep << " asleep; awake:";
    const char* separator = " ";
    for (const auto& [reason, count] : awake)
    {
      std::cout << separator << count << " " << reason;
      separator = ", ";
    }
    std::cout << "\n";
  }
}

// The Terrain Basin scene stepped as the server steps it, timed a second at a time, with how many of its bodies moved.
// A measurement, not a check: run it on purpose with --gtest_also_run_disabled_tests, in a Release build. See
// testing.md for timing the single-threaded path. The stacks are placed at random each run.
TEST(BasinBenchmark, DISABLED_TimesTheTerrainBasinTickByTick)
{
  const Basin world;
  ASSERT_NE(world.scene, nullptr);
  ASSERT_EQ(world.boundTiles, basin::tileCount);
  ASSERT_GT(world.bodies.size(), 600u);

  CollisionSystem collisionSystem;
  auto before = world.poses();
  double totalMilliseconds = 0.0;
  double settledMilliseconds = 0.0;

  for (int second = 0; second < seconds; ++second)
  {
    const auto timing = runSecond(world, collisionSystem, before);
    totalMilliseconds += timing.milliseconds;
    settledMilliseconds += second >= seconds - settledSeconds ? timing.milliseconds : 0.0;

    std::cout << "[basin] second " << second + 1 << ": " << timing.milliseconds / ticksPerSecond
              << " ms per tick (worst " << timing.worstTick << "), "
              << static_cast<double>(timing.moved) / ticksPerSecond << " of " << world.bodies.size()
              << " bodies moved per tick\n";
  }

  reportAwake(world.bodies);
  std::cout << "[basin] mean " << totalMilliseconds / (seconds * ticksPerSecond) << " ms per tick, last "
            << settledSeconds << " seconds " << settledMilliseconds / (settledSeconds * ticksPerSecond)
            << " ms per tick, budget " << tick * 1000.0f << " ms\n";
}
