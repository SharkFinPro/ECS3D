#include <gtest/gtest.h>

#include "PhysicsPlaygroundTestHelpers.h"
#include "TemplateTestHelpers.h"
#include "collisions/NarrowPhase.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/collisions/Collider.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace templateTest;
using namespace playgroundtest;
using nlohmann::json;

namespace
{
  struct SpawnerExpectation
  {
    std::string object;
    std::string prefab;
    float interval;
    int maxAlive;
    float radius;
    int seed;
  };

  void expectSpawner(const SpawnerExpectation& expected)
  {
    SCOPED_TRACE(expected.object);
    const auto& spawner = only(expected.object);

    EXPECT_EQ(scriptField(spawner, "Spawner", "m_prefab").get<std::string>(), prefabUuid(expected.prefab));
    EXPECT_FLOAT_EQ(scriptField(spawner, "Spawner", "m_interval").get<float>(), expected.interval);
    EXPECT_EQ(scriptField(spawner, "Spawner", "m_maxAlive").get<int>(), expected.maxAlive);
    EXPECT_FLOAT_EQ(scriptField(spawner, "Spawner", "m_offsetRadius").get<float>(), expected.radius);

    if (expected.seed != 0)
    {
      EXPECT_EQ(scriptField(spawner, "Spawner", "m_seed").get<int>(), expected.seed);
    }
  }

  bool isAncestor(const std::shared_ptr<Object>& ancestor, std::shared_ptr<Object> object)
  {
    for (auto parent = object->getParent(); parent; parent = parent->getParent())
    {
      if (parent == ancestor)
      {
        return true;
      }
    }

    return false;
  }

  bool hasBody(const std::shared_ptr<Object>& object)
  {
    return object->getComponent<RigidBody>(ComponentType::rigidBody) != nullptr;
  }

  bool shouldCheck(const std::shared_ptr<Object>& a, const std::shared_ptr<Object>& b)
  {
    const bool related = isAncestor(a, b) || isAncestor(b, a);

    return (hasBody(a) || hasBody(b)) && !related;
  }

  float depthOf(const std::shared_ptr<Object>& a, const std::shared_ptr<Object>& b)
  {
    const auto contact = collisions::findContact(*a->getComponent<Collider>(ComponentType::collider),
                                                 *b->getComponent<Collider>(ComponentType::collider));
    if (!contact)
    {
      return 0.0f;
    }

    return contact->depth();
  }
}

TEST(PhysicsPlaygroundSampleSpawners, RaceLanesSpawnTheirPrefabsInOrderAndOnlyRollersGetAPush)
{
  const std::vector<std::pair<std::string, std::string>> lanes = {
    { "Race Spawner Sphere", "Sphere" }, { "Race Spawner Drum", "Drum Rolling" },
    { "Race Spawner Hex Log", "Hex Log" }, { "Race Spawner Capsule Across", "Capsule Across" },
    { "Race Spawner Capsule Along", "Capsule Along" }, { "Race Spawner Block", "Ramp Block" }
  };

  float previousX = -1000.0f;
  for (const auto& [object, prefab] : lanes)
  {
    expectSpawner({ object, prefab, 10.0f, 1, 0.0f, 0 });
    const auto x = positionOfJson(only(object)).x;
    EXPECT_GT(x, previousX) << object;
    previousX = x;

    const bool pushed = object == "Race Spawner Drum" || object == "Race Spawner Hex Log";
    const auto& fields = only(object).at("scripts").at(0).at("fields");
    const bool hasVelocity = std::ranges::any_of(fields, [](const json& field) {
      return field.at("name") == "m_initialVelocity";
    });
    EXPECT_EQ(hasVelocity, pushed) << object;
  }

  const auto velocity = vec3Of(scriptField(only("Race Spawner Drum"), "Spawner", "m_initialVelocity"));
  EXPECT_NEAR(glm::length(velocity), 3.0f, 1e-3f);
  EXPECT_LT(velocity.y, 0.0f);
  EXPECT_GT(velocity.z, 0.0f);
  EXPECT_FLOAT_EQ(velocity.x, 0.0f);
}

TEST(PhysicsPlaygroundSampleSpawners, RainSpawnersDropTheirOwnSmallShapeWithTheirOwnSeed)
{
  const std::vector<SpawnerExpectation> rain = {
    { "Rain Spawner D4", "Small D4", 1.25f, 12, 4.0f, 1 }, { "Rain Spawner D8", "Small D8", 1.25f, 12, 4.0f, 2 },
    { "Rain Spawner D12", "Small D12", 1.25f, 12, 4.0f, 3 }, { "Rain Spawner D20", "Small D20", 1.25f, 12, 4.0f, 4 },
    { "Rain Spawner Gem", "Small Gem", 1.25f, 12, 4.0f, 5 }
  };

  for (const auto& expected : rain)
  {
    expectSpawner(expected);
  }
}

TEST(PhysicsPlaygroundSampleSpawners, EachBounceBallSpawnerSitsAboveItsOwnPad)
{
  for (const std::string strength : { "8", "14", "20" })
  {
    const auto& spawner = only("Ball Spawner " + strength);
    const auto& pad = only("Bounce Pad " + strength);

    expectSpawner({ "Ball Spawner " + strength, "Small Ball", 2.0f, 3, 0.5f, 0 });
    EXPECT_FLOAT_EQ(positionOfJson(spawner).x, positionOfJson(pad).x);
    EXPECT_FLOAT_EQ(positionOfJson(spawner).z, positionOfJson(pad).z);
    EXPECT_GT(positionOfJson(spawner).y, positionOfJson(pad).y + 3.0f);
  }
}

TEST(PhysicsPlaygroundSampleSpawners, PuckSpawnersReleaseOneAtATimeFromTheSlopeTop)
{
  expectSpawner({ "Puck Spawner Ice", "Puck Ice", 8.0f, 1, 0.0f, 0 });
  expectSpawner({ "Puck Spawner Wood", "Puck Wood", 8.0f, 1, 0.0f, 0 });
  expectSpawner({ "Puck Spawner Rubber", "Puck Rubber", 8.0f, 1, 0.0f, 0 });

  EXPECT_LT(positionOfJson(only("Puck Spawner Ice")).z, positionOfJson(only("Puck Spawner Wood")).z);
  EXPECT_LT(positionOfJson(only("Puck Spawner Wood")).z, positionOfJson(only("Puck Spawner Rubber")).z);
}

TEST(PhysicsPlaygroundSampleSpawners, NoDynamicColliderStartsOverlappingAnotherByMoreThanAMillimeter)
{
  LoadedTemplate loaded(sampleFolder);
  ASSERT_TRUE(loaded.loaded);
  const auto& objects = loaded.sceneManager.getCurrentScene()->getObjectManager()->getAllObjects();

  std::vector<std::shared_ptr<Object>> withColliders;
  for (const auto& object : objects)
  {
    if (object->getComponent<Collider>(ComponentType::collider))
    {
      withColliders.push_back(object);
    }
  }

  ASSERT_GT(withColliders.size(), 140u);

  int dynamicPairs = 0;
  for (std::size_t i = 0; i < withColliders.size(); ++i)
  {
    for (std::size_t j = i + 1; j < withColliders.size(); ++j)
    {
      if (!shouldCheck(withColliders[i], withColliders[j]))
      {
        continue;
      }

      ++dynamicPairs;
      EXPECT_LT(depthOf(withColliders[i], withColliders[j]), 1e-3f)
        << withColliders[i]->getName() << " / " << withColliders[j]->getName();
    }
  }

  EXPECT_GT(dynamicPairs, 5000);
}
