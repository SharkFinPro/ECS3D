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

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <uuid.h>
#include <vector>

namespace {
  constexpr float tick = 1.0f / 50.0f;
  constexpr int ticksPerSecond = 50;
  constexpr int seconds = 20;

  struct Pose {
    glm::vec3 position;
    glm::quat orientation;

    bool operator==(const Pose&) const = default;
  };
}

// The Terrain Basin scene stepped as the server steps it, timed a second at a time, with how many of its bodies moved.
// A measurement, not a check: run it on purpose with --gtest_also_run_disabled_tests, in a Release build, and set
// OMP_NUM_THREADS=1 to time the single-threaded path. The stacks are placed at random each run.
TEST(BasinBenchmark, DISABLED_TimesTheTerrainBasinTickByTick)
{
  const auto componentRegistry = std::make_shared<ComponentRegistry>();
  registerDataComponents(*componentRegistry);
  AssetRegistry assetRegistry;
  SceneManager sceneManager;
  const ProjectSerializer serializer(&assetRegistry, &sceneManager, componentRegistry);
  serializer.deserialize(buildDefaultProject());

  std::shared_ptr<SceneAsset> scene;
  for (const auto& [uuid, candidate] : sceneManager.getScenes())
  {
    if (candidate->getName() == "Terrain Basin")
    {
      scene = candidate;
    }
  }

  ASSERT_NE(scene, nullptr);
  sceneManager.loadScene(scene);
  const auto& objectManager = *scene->getObjectManager();

  std::array<std::shared_ptr<terrain::TerrainVolume>, basin::tileCount> tiles;
  std::array<terrain::TerrainVolume*, basin::tileCount> pointers{};
  for (size_t i = 0; i < tiles.size(); ++i)
  {
    tiles[i] = std::make_shared<terrain::TerrainVolume>(basin::voxelSize);
    pointers[i] = tiles[i].get();
  }
  generateBasinTiles(pointers);

  int bound = 0;
  std::vector<std::shared_ptr<Transform>> bodies;
  for (const auto& object : objectManager.getAllObjects())
  {
    if (const auto collider = object->getComponent<Collider>(ComponentType::collider);
        collider && collider->getColliderType() == ColliderType::terrainCollider)
    {
      auto* terrain = static_cast<Terrain*>(collider.get());
      const auto uuid = uuids::to_string(terrain->getTerrainUUID());
      const auto found = std::ranges::find(defaultproject::basinTerrains, uuid);
      if (found != defaultproject::basinTerrains.end())
      {
        terrain->setVolume(tiles[static_cast<size_t>(found - defaultproject::basinTerrains.begin())]);
        ++bound;
      }
    }

    const auto body = object->getComponent<RigidBody>(ComponentType::rigidBody);
    if (body && body->getOwner() == object.get())
    {
      bodies.push_back(object->getComponent<Transform>(ComponentType::transform));
    }
  }

  ASSERT_EQ(bound, basin::tileCount);
  ASSERT_GT(bodies.size(), 600u);

  const auto poses = [&bodies]
  {
    std::vector<Pose> result;
    result.reserve(bodies.size());
    for (const auto& transform : bodies)
    {
      result.push_back({ transform->getPosition(), transform->getOrientation() });
    }

    return result;
  };

  CollisionSystem collisionSystem;
  auto before = poses();
  double totalMilliseconds = 0.0;
  double settledMilliseconds = 0.0;

  for (int second = 0; second < seconds; ++second)
  {
    std::chrono::duration<double, std::milli> spent{ 0 };
    double worst = 0.0;
    size_t moved = 0;

    for (int i = 0; i < ticksPerSecond; ++i)
    {
      const auto start = std::chrono::steady_clock::now();
      PhysicsSystem::fixedUpdate(objectManager, tick);
      collisionSystem.fixedUpdate(objectManager, tick);
      const std::chrono::duration<double, std::milli> one = std::chrono::steady_clock::now() - start;
      spent += one;
      worst = std::max(worst, one.count());

      const auto after = poses();
      for (size_t b = 0; b < after.size(); ++b)
      {
        moved += after[b] == before[b] ? 0 : 1;
      }
      before = after;
    }

    const double perTick = spent.count() / ticksPerSecond;
    totalMilliseconds += spent.count();
    if (second >= seconds - 5)
    {
      settledMilliseconds += spent.count();
    }

    std::cout << "[basin] second " << second + 1 << ": " << perTick << " ms per tick (worst " << worst << "), "
              << static_cast<double>(moved) / ticksPerSecond << " of " << bodies.size() << " bodies moved per tick\n";
  }

  std::cout << "[basin] mean " << totalMilliseconds / (seconds * ticksPerSecond) << " ms per tick, last five seconds "
            << settledMilliseconds / (5 * ticksPerSecond) << " ms per tick, budget " << tick * 1000.0f << " ms\n";
}
