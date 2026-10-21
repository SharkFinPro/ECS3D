#include <gtest/gtest.h>

#include "PhysicsPlaygroundTestHelpers.h"
#include "TemplateTestHelpers.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/LightRenderer.h"
#include "objects/components/Script.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"
#include "templates/TemplateManifest.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace templateTest;
using namespace playgroundtest;
using nlohmann::json;

namespace
{
  glm::vec3 vertexAt(const std::vector<float>& flat, const int index)
  {
    return { flat[index * 3], flat[index * 3 + 1], flat[index * 3 + 2] };
  }

  float hullVolume(const json& collider)
  {
    const auto flat = collider.at("vertices").get<std::vector<float>>();
    const auto indices = collider.at("indices").get<std::vector<int>>();
    float volume = 0.0f;
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
    {
      volume += glm::dot(vertexAt(flat, indices[i]),
                         glm::cross(vertexAt(flat, indices[i + 1]), vertexAt(flat, indices[i + 2]))) / 6.0f;
    }

    return volume;
  }

  float expectedMass(const json& body)
  {
    const auto scale = scaleOfJson(body);
    const float product = scale.x * scale.y * scale.z;
    const auto subType = componentOf(body, "Collider").at("subType").get<std::string>();

    if (subType == "ConvexMesh")
    {
      return hullVolume(componentOf(body, "Collider", "ConvexMesh")) * product;
    }

    if (subType == "Box")
    {
      return 8.0f * product;
    }

    if (subType == "Sphere")
    {
      return 4.18879f * product;
    }

    // A capsule: the two caps and the cylinder between them, at scale one.
    return 4.0f / 3.0f * 3.14159265f + 2.0f * 3.14159265f;
  }

  std::vector<const json*> dominoes()
  {
    std::vector<const json*> found;
    for (const auto* object : named("Domino "))
    {
      if (hasComponent(*object, "RigidBody") && object->at("name") != "Domino Sphere")
      {
        found.push_back(object);
      }
    }

    return found;
  }
}

TEST(PhysicsPlaygroundSample, ManifestParsesWithTheShippedFields)
{
  std::string error;
  const auto manifest = loadTemplateManifest(sampleFolder, &error);
  ASSERT_TRUE(manifest.has_value()) << error;

  EXPECT_EQ(manifest->id, "physics-playground");
  EXPECT_EQ(manifest->name, "Physics Playground");
  EXPECT_EQ(manifest->category, "sample");
  EXPECT_EQ(manifest->tags, (std::vector<std::string>{ "physics", "scripting" }));
  EXPECT_EQ(manifest->thumbnail, "thumbnail.png");
  EXPECT_EQ(manifest->entryScene, "Playground");
  EXPECT_EQ(manifest->minEngineVersion, "0.0.1");
  EXPECT_FALSE(manifest->description.empty());
  EXPECT_FALSE(manifest->players.has_value());
}

TEST(PhysicsPlaygroundSample, ProjectLoadsWithPlaygroundAsTheCurrentScene)
{
  LoadedTemplate loaded(sampleFolder);
  ASSERT_TRUE(loaded.loaded);

  const auto scene = loaded.sceneManager.getCurrentScene();
  ASSERT_NE(scene, nullptr);
  EXPECT_EQ(scene->getName(), "Playground");
  EXPECT_EQ(loaded.sceneManager.getScenes().size(), 1u);
}

TEST(PhysicsPlaygroundSample, GroundIsOneHundredTwentyByEightyOfTwentyFourTiles)
{
  const auto ground = named("Ground");
  ASSERT_EQ(ground.size(), 1u + 24u);
  const auto& root = *ground.front();

  EXPECT_EQ(positionOfJson(root), glm::vec3(0, -1, 0));
  EXPECT_EQ(vec3Of(componentOf(root, "Collider", "Box").at("scale")), glm::vec3(60, 1, 40));
  EXPECT_FALSE(componentOf(root, "ModelRenderer").at("shouldRender").get<bool>());
  ASSERT_EQ(root.at("children").size(), 24u);

  std::set<std::pair<float, float>> corners;
  for (const auto& tile : root.at("children"))
  {
    EXPECT_EQ(scaleOfJson(tile), glm::vec3(10, 1, 10));
    corners.emplace(positionOfJson(tile).x, positionOfJson(tile).z);
  }

  EXPECT_EQ(corners.size(), 24u);
  EXPECT_TRUE(corners.contains({ -50.0f, -30.0f }));
  EXPECT_TRUE(corners.contains({ 50.0f, 30.0f }));
}

TEST(PhysicsPlaygroundSample, ExactlyOneSunAbovePlaygroundCenter)
{
  LoadedTemplate loaded(sampleFolder);
  ASSERT_TRUE(loaded.loaded);

  int lights = 0;
  for (const auto& object : loaded.sceneManager.getCurrentScene()->getObjectManager()->getAllObjects())
  {
    lights += object->getComponent<LightRenderer>(ComponentType::lightRenderer) ? 1 : 0;
  }

  EXPECT_EQ(lights, 1);
  const auto sun = named("Sun");
  ASSERT_EQ(sun.size(), 1u);
  EXPECT_GE(positionOfJson(*sun.front()).y, 20.0f);
  EXPECT_LE(positionOfJson(*sun.front()).y, 40.0f);
}

TEST(PhysicsPlaygroundSample, EveryStationRootHoldsItsFlatPad)
{
  const std::vector<std::string> names = { "Domino run", "Pyramid", "Cradle row", "Friction slopes", "Bounce pads",
                                           "Ramp race", "Explosion pit", "Shape rain", "Hub" };
  for (const auto& name : names)
  {
    SCOPED_TRACE(name);
    const auto roots = named("Station: " + name);
    ASSERT_EQ(roots.size(), 1u);

    const auto& pad = roots.front()->at("children").at(0);
    EXPECT_EQ(pad.at("name"), "Pad");
    EXPECT_NEAR(scaleOfJson(pad).y, 0.025f, 1e-6f);
    EXPECT_NEAR(positionOfJson(pad).y, 0.025f, 1e-6f);
    EXPECT_FALSE(hasComponent(pad, "RigidBody"));
  }

  EXPECT_EQ(positionOfJson(*named("Station: Domino run").front()), glm::vec3(-38, 0, -26));
  EXPECT_EQ(positionOfJson(*named("Station: Hub").front()), glm::vec3(0, 0, 2));
  EXPECT_EQ(named("Station: ").size(), names.size());
}

TEST(PhysicsPlaygroundSample, DominoRunHasThirtyDominoesAtSixTenthsMass)
{
  const auto found = dominoes();

  for (const auto* domino : found)
  {
    EXPECT_EQ(scaleOfJson(*domino), glm::vec3(0.15f, 1.0f, 0.5f));
    EXPECT_NEAR(componentOf(*domino, "RigidBody").at("mass").get<float>(), 0.6f, 1e-5f);
  }

  EXPECT_EQ(found.size(), 30u);
}

TEST(PhysicsPlaygroundSample, PyramidIsThirtyCubesInLayersOfSixteenNineFourOne)
{
  const auto cubes = named("Pyramid Cube");
  ASSERT_EQ(cubes.size(), 30u);

  std::map<float, int> layers;
  for (const auto* cube : cubes)
  {
    layers[std::round(positionOfJson(*cube).y * 1000.0f) / 1000.0f]++;
    EXPECT_NEAR(componentOf(*cube, "RigidBody").at("mass").get<float>(), 1.0f, 1e-5f);
  }

  const std::vector<int> expected = { 16, 9, 4, 1 };
  ASSERT_EQ(layers.size(), expected.size());

  // Each layer's bottom is the top of the one below; the lowest sits on the 0.05 pad.
  std::size_t layer = 0;
  float below = 0.05f;
  for (const auto& [height, count] : layers)
  {
    EXPECT_EQ(count, expected[layer++]);
    EXPECT_NEAR(height - 0.5f, below, 1e-4f);
    below = height + 0.5f;
  }
}

TEST(PhysicsPlaygroundSample, CradleHasSevenTouchingSpheresAndOneFeeder)
{
  const auto spheres = named("Cradle Sphere ");
  ASSERT_EQ(spheres.size(), 7u);

  std::vector<float> xs;
  for (const auto* sphere : spheres)
  {
    xs.push_back(positionOfJson(*sphere).x);
    EXPECT_NEAR(positionOfJson(*sphere).y, 0.55f, 1e-5f);
  }

  std::sort(xs.begin(), xs.end());
  for (std::size_t i = 1; i < xs.size(); ++i)
  {
    EXPECT_NEAR(xs[i] - xs[i - 1], 1.0f, 1e-5f);
  }

  EXPECT_EQ(named("Cradle Feeder Sphere").size(), 1u);
  EXPECT_EQ(named("Feeder Ramp").size(), 1u);
}

TEST(PhysicsPlaygroundSample, FrictionBounceAndRaceSpawnersAreConfigured)
{
  const std::map<std::string, float> pucks = { { "Ice", 0.0f }, { "Wood", 0.5f }, { "Rubber", 1.0f } };
  const auto prefabs = prefabsByUuid();
  for (const auto& [name, friction] : pucks)
  {
    const auto spawn = named("Puck Spawner " + name);
    ASSERT_EQ(spawn.size(), 1u);
    const auto uuid = scriptField(*spawn.front(), "Spawner", "m_prefab").get<std::string>();
    ASSERT_TRUE(prefabs.contains(uuid));
    EXPECT_FLOAT_EQ(componentOf(prefabs.at(uuid).at("body"), "RigidBody").at("friction").get<float>(), friction)
      << name;
    EXPECT_EQ(scriptField(*spawn.front(), "Spawner", "m_maxAlive"), 1);
  }

  std::set<float> strengths;
  for (const auto* pad : named("Bounce Pad "))
  {
    strengths.insert(scriptField(*pad, "BouncePad", "m_strength").get<float>());
    EXPECT_FALSE(scriptField(*pad, "ColorOnHit", "m_hitTexture").get<std::string>().empty());
  }

  EXPECT_EQ(strengths, (std::set<float>{ 8.0f, 14.0f, 20.0f }));
  EXPECT_EQ(named("Ball Spawner ").size(), 3u);
  EXPECT_EQ(named("Race Spawner ").size(), 6u);
}

TEST(PhysicsPlaygroundSample, ExplosionHeapAndShapeRainMatchTheirStations)
{
  EXPECT_EQ(named("Heap ").size(), 25u);

  const auto explosion = named("Explosion");
  ASSERT_EQ(explosion.size(), 1u);
  EXPECT_EQ(scriptField(*explosion.front(), "Explosion", "m_key"), "E");
  EXPECT_FLOAT_EQ(scriptField(*explosion.front(), "Explosion", "m_radius").get<float>(), 7.0f);

  std::set<int> seeds;
  const auto rain = named("Rain Spawner ");
  for (const auto* spawner : rain)
  {
    seeds.insert(scriptField(*spawner, "Spawner", "m_seed").get<int>());
    EXPECT_EQ(scriptField(*spawner, "Spawner", "m_maxAlive"), 12);
    EXPECT_FLOAT_EQ(scriptField(*spawner, "Spawner", "m_interval").get<float>(), 1.25f);
    EXPECT_FLOAT_EQ(positionOfJson(*spawner).y, 25.0f);
  }

  EXPECT_EQ(rain.size(), 5u);
  EXPECT_EQ(seeds, (std::set<int>{ 1, 2, 3, 4, 5 }));
}

TEST(PhysicsPlaygroundSample, EverySpawnerNamesARegisteredPrefab)
{
  const auto prefabs = prefabsByUuid();
  int spawners = 0;
  for (const auto* object : allObjects())
  {
    if (object->at("scripts").empty() || object->at("scripts").at(0).at("className") != "Spawner")
    {
      continue;
    }

    ++spawners;
    EXPECT_TRUE(prefabs.contains(scriptField(*object, "Spawner", "m_prefab").get<std::string>()))
      << object->at("name");
  }

  EXPECT_EQ(spawners, 3 + 3 + 6 + 5);
}

TEST(PhysicsPlaygroundSample, PrefabMassesEqualTheirScaledVolume)
{
  int checked = 0;
  for (const auto& [uuid, prefab] : prefabsByUuid())
  {
    const auto& body = prefab.at("body");
    if (!hasComponent(body, "RigidBody"))
    {
      continue;
    }

    EXPECT_NEAR(componentOf(body, "RigidBody").at("mass").get<float>(), expectedMass(body), 2e-3f)
      << prefab.at("name");
    ++checked;
  }

  EXPECT_GE(checked, 14);
}

TEST(PhysicsPlaygroundSample, HubHoldsPlayerZeroAndTheSceneScript)
{
  LoadedTemplate loaded(sampleFolder);
  ASSERT_TRUE(loaded.loaded);
  const auto& scene = *loaded.sceneManager.getCurrentScene();

  ASSERT_EQ(scene.getScripts().size(), 1u);
  EXPECT_EQ(scene.getScripts().front()->getClassName(), "PlayerSpawnManager");
  EXPECT_EQ(named("SpawnPoint").size(), 4u);

  const auto player = named("Player 0");
  ASSERT_EQ(player.size(), 1u);
  EXPECT_EQ(componentOf(*player.front(), "PlayerController").at("playerSlot"), 0);
  EXPECT_EQ(vec3Of(componentOf(*player.front(), "Camera").at("direction")), glm::vec3(0, 0, -1));
}

namespace
{
  const json* dominoNumbered(const int number)
  {
    for (const auto* object : dominoes())
    {
      if (object->at("name") == "Domino " + std::to_string(number))
      {
        return object;
      }
    }

    return nullptr;
  }

  float planDistance(const json& a, const json& b)
  {
    const auto delta = positionOfJson(a) - positionOfJson(b);
    return std::hypot(delta.x, delta.z);
  }
}

TEST(PhysicsPlaygroundSample, ConsecutiveDominoesAreTwelveTenthsApart)
{
  for (int number = 2; number < 30; ++number)
  {
    const auto* current = dominoNumbered(number);
    const auto* next = dominoNumbered(number + 1);
    ASSERT_NE(current, nullptr);
    ASSERT_NE(next, nullptr);
    EXPECT_NEAR(planDistance(*current, *next), 1.2f, 1e-3f) << number;
  }

  // The first leans about its front edge, so its center sits a little ahead of its slot.
  EXPECT_NEAR(planDistance(*dominoNumbered(1), *dominoNumbered(2)), 1.2f - 0.264f, 1e-2f);
}

TEST(PhysicsPlaygroundSample, TheLastDominoStandsOnTheLedgeWithTheSphereAhead)
{
  const auto& ledge = only("Domino Ledge");
  const float ledgeTop = positionOfJson(ledge).y + scaleOfJson(ledge).y;
  const auto* last = dominoNumbered(30);
  ASSERT_NE(last, nullptr);

  EXPECT_NEAR(ledgeTop, 0.85f, 1e-5f);
  EXPECT_NEAR(positionOfJson(*last).y, ledgeTop + 1.0f, 1e-4f);
  EXPECT_LT(planDistance(*last, ledge), scaleOfJson(ledge).x);

  // The sphere rests on the same ledge, ahead of the domino and no further than the ledge end.
  const auto& sphere = only("Domino Sphere");
  EXPECT_NEAR(positionOfJson(sphere).y, ledgeTop + 0.5f, 1e-4f);
  EXPECT_GT(planDistance(sphere, *last), 0.8f);
  EXPECT_LT(planDistance(sphere, ledge), scaleOfJson(ledge).x);

  // Positive control: an upright domino on the ground is lower by the ledge height.
  EXPECT_NEAR(positionOfJson(*dominoNumbered(29)).y, 1.05f, 1e-4f);
}
