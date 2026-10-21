#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "DefaultShapes.h"
#include "PhysicsSystem.h"
#include "TemplateTestHelpers.h"
#include "TestScene.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"
#include "templates/TemplateManifest.h"

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <uuid.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace templateTest;

namespace
{
  const std::filesystem::path templateFolder = sourceRoot / "templates" / "coin-rush";

  constexpr const char* gridCheckerTexture = "48ad67dd-957f-4fc9-bd7b-019debffa1d0";
  constexpr const char* torusModel = "7a1cd753-3775-44a0-858f-27239f8740f5";
  constexpr const char* paletteYellowTexture = "1d5edebf-57f9-499c-9840-b84a57174422";
  constexpr const char* playerPrefab = "9c7f0e83-4d16-4b52-a09e-27f5b3d81c6a";
  constexpr float dt = 1.0f / 50.0f;

  // Palette red, blue, green and orange: the textures of player slots 0 to 3.
  const std::vector<std::string> slotTextures = {
    "7c8c01db-771f-4048-94c2-004b8d19e7aa", "66a8a6fc-d151-4567-9ce5-2e17cc675fe0",
    "c5eee5e6-9bd0-4f9b-977f-b9220a21a630", "ea184ad8-ca64-48cd-b69e-92bcb4649158"
  };

  bool approx(const float a, const float b)
  {
    return std::abs(a - b) < 1e-4f;
  }

  std::set<std::string> rootNames(const SceneAsset& scene)
  {
    std::set<std::string> names;
    for (const auto& object : scene.getObjectManager()->getObjects())
    {
      names.insert(object->getName());
    }

    return names;
  }

  nlohmann::json scriptNamed(const nlohmann::json& object, const std::string& className)
  {
    for (const auto& entry : object.at("scripts"))
    {
      if (entry.at("className") == className)
      {
        return entry;
      }
    }

    ADD_FAILURE() << object.at("name") << " has no script " << className;
    return nlohmann::json::object();
  }

  nlohmann::json fieldOf(const nlohmann::json& script, const std::string& name)
  {
    for (const auto& field : script.at("fields"))
    {
      if (field.at("name") == name)
      {
        return field.at("value");
      }
    }

    ADD_FAILURE() << script.at("className") << " has no field " << name;
    return nullptr;
  }

  nlohmann::json prefabBody(const LoadedTemplate& project, const std::string& uuid)
  {
    return project.assetRegistry.getPrefabBody(uuids::uuid::from_string(uuid).value());
  }

  nlohmann::json prefabNamed(const nlohmann::json& project, const std::string& name)
  {
    for (const auto& prefab : project.at("assets").at("prefabs"))
    {
      if (prefab.at("name") == name)
      {
        return prefab;
      }
    }

    ADD_FAILURE() << "no prefab named " << name;
    return nlohmann::json::object();
  }

  std::string textureOf(const nlohmann::json& object)
  {
    return componentOf(object, "ModelRenderer").at("textureUUID").get<std::string>();
  }
}

TEST(CoinRushSample, ManifestParsesWithTheShippedFields)
{
  std::string error;
  const auto manifest = loadTemplateManifest(templateFolder, &error);
  ASSERT_TRUE(manifest.has_value()) << error;

  EXPECT_EQ(manifest->id, "coin-rush");
  EXPECT_EQ(manifest->name, "Coin Rush");
  EXPECT_EQ(manifest->category, "sample");
  EXPECT_EQ(manifest->tags, (std::vector<std::string>{ "multiplayer", "scripting", "game" }));
  EXPECT_EQ(manifest->thumbnail, "thumbnail.png");
  EXPECT_EQ(manifest->entryScene, "Arena");
  EXPECT_EQ(manifest->minEngineVersion, "0.0.1");
  ASSERT_TRUE(manifest->players.has_value());
  EXPECT_EQ(manifest->players->min, 1);
  EXPECT_EQ(manifest->players->max, 4);
}

TEST(CoinRushSample, ProjectLoadsWithArenaAsTheCurrentScene)
{
  LoadedTemplate project(templateFolder);
  ASSERT_TRUE(project.loaded);

  const auto scene = project.sceneManager.getCurrentScene();
  ASSERT_NE(scene, nullptr);
  EXPECT_EQ(scene->getName(), "Arena");
  EXPECT_EQ(project.sceneManager.getScenes().size(), 1u);
}

TEST(CoinRushSample, FloorIsSixtyBySixtyOfNineGridTiles)
{
  LoadedTemplate project(templateFolder);
  ASSERT_TRUE(project.loaded);
  const auto ground = objectNamed(*project.sceneManager.getCurrentScene(), "Ground");

  EXPECT_EQ(positionOfJson(ground), glm::vec3(0, -1, 0));
  EXPECT_EQ(vec3Of(componentOf(ground, "Collider", "Box").at("scale")), glm::vec3(30, 1, 30));
  EXPECT_FALSE(componentOf(ground, "ModelRenderer").at("shouldRender").get<bool>());

  ASSERT_EQ(ground.at("children").size(), 9u);
  std::set<std::pair<float, float>> corners;
  for (const auto& tile : ground.at("children"))
  {
    EXPECT_EQ(scaleOfJson(tile), glm::vec3(10, 1, 10));
    EXPECT_EQ(componentOf(tile, "ModelRenderer").at("textureUUID"), gridCheckerTexture);
    corners.emplace(positionOfJson(tile).x, positionOfJson(tile).z);
  }

  EXPECT_EQ(corners.size(), 9u);
  EXPECT_TRUE(corners.contains({ -20, -20 }));
  EXPECT_TRUE(corners.contains({ 20, 20 }));
}

TEST(CoinRushSample, HasExactlyOneSun)
{
  LoadedTemplate project(templateFolder);
  ASSERT_TRUE(project.loaded);
  const auto& scene = *project.sceneManager.getCurrentScene();

  int lights = 0;
  for (const auto& object : sceneObjects(scene))
  {
    lights += hasComponent(object, "LightRenderer") ? 1 : 0;
  }

  EXPECT_EQ(lights, 1);
  const auto light = componentOf(objectNamed(scene, "Sun"), "LightRenderer");
  EXPECT_FALSE(light.at("isSpotlight").get<bool>());
  EXPECT_NEAR(light.at("ambient").get<float>(), 0.25f, 1e-6f);
}

TEST(CoinRushSample, SpawnDiscsSitAtTheCornersInTheSlotColors)
{
  LoadedTemplate project(templateFolder);
  ASSERT_TRUE(project.loaded);
  const auto& scene = *project.sceneManager.getCurrentScene();

  const std::vector<glm::vec3> corners = { { -24, 0.02f, -24 }, { 24, 0.02f, -24 }, { 24, 0.02f, 24 },
                                           { -24, 0.02f, 24 } };
  EXPECT_EQ(objectsStartingWith(scene, "SpawnPoint").size(), 4u);

  for (std::size_t slot = 0; slot < corners.size(); ++slot)
  {
    const auto disc = objectNamed(scene, "SpawnPoint" + std::to_string(slot + 1));
    EXPECT_EQ(positionOfJson(disc), corners[slot]);
    EXPECT_EQ(textureOf(disc), slotTextures[slot]);
    EXPECT_FALSE(hasComponent(disc, "Collider"));
    EXPECT_FALSE(hasComponent(disc, "RigidBody"));
  }
}

TEST(CoinRushSample, ScorePillarsAreColliderlessRootsInTheirSlotColors)
{
  LoadedTemplate project(templateFolder);
  ASSERT_TRUE(project.loaded);
  const auto& scene = *project.sceneManager.getCurrentScene();
  const auto discs = objectsStartingWith(scene, "SpawnPoint");
  const auto pillars = objectsStartingWith(scene, "Score Pillar");
  ASSERT_EQ(pillars.size(), 4u);

  std::set<int> slots;
  for (const auto& pillar : pillars)
  {
    SCOPED_TRACE(pillar.at("name").get<std::string>());
    EXPECT_FALSE(hasComponent(pillar, "Collider"));
    EXPECT_FALSE(hasComponent(pillar, "RigidBody"));
    EXPECT_TRUE(rootNames(scene).contains(pillar.at("name")));

    const int slot = fieldOf(scriptNamed(pillar, "ScorePillar"), "m_slot").get<int>();
    slots.insert(slot);
    EXPECT_EQ(textureOf(pillar), slotTextures.at(static_cast<std::size_t>(slot)));

    const auto position = positionOfJson(pillar);
    const auto nearest = *std::ranges::min_element(discs, {}, [&](const nlohmann::json& disc) {
      return glm::distance(glm::vec2(positionOfJson(disc).x, positionOfJson(disc).z), glm::vec2(position.x, position.z));
    });
    EXPECT_EQ(textureOf(nearest), textureOf(pillar));
    EXPECT_TRUE(approx(position.y - scaleOfJson(pillar).y, 0.0f));
  }

  EXPECT_EQ(slots, (std::set<int>{ 0, 1, 2, 3 }));
}

TEST(CoinRushSample, DaisTopIsTwoAndEachRampMeetsItsEdge)
{
  LoadedTemplate project(templateFolder);
  ASSERT_TRUE(project.loaded);
  const auto& scene = *project.sceneManager.getCurrentScene();
  const auto dais = objectNamed(scene, "Dais");

  EXPECT_TRUE(approx(positionOfJson(dais).y + scaleOfJson(dais).y, 2.0f));
  EXPECT_EQ(scaleOfJson(dais).x, 6.0f);
  EXPECT_EQ(scaleOfJson(dais).z, 6.0f);

  const auto ramps = objectsStartingWith(scene, "Ramp ");
  ASSERT_EQ(ramps.size(), 4u);
  for (const auto& ramp : ramps)
  {
    SCOPED_TRACE(ramp.at("name").get<std::string>());
    EXPECT_FALSE(hasComponent(ramp, "RigidBody"));
    int high = 0;
    int farLow = 0;
    for (const auto& vertex : worldHullVertices(ramp))
    {
      const float reach = std::max(std::abs(vertex.x), std::abs(vertex.z));
      const float along = std::min(std::abs(vertex.x), std::abs(vertex.z));
      high += approx(vertex.y, 2.0f) ? 1 : 0;
      farLow += approx(vertex.y, 0.0f) && approx(reach, 14.0f) ? 1 : 0;
      EXPECT_GE(vertex.y, -1e-4f);
      EXPECT_TRUE(approx(vertex.y, 2.0f) ? approx(reach, 6.0f) : (approx(reach, 6.0f) || approx(reach, 14.0f)));
      EXPECT_LE(along, 6.0f + 1e-4f);
    }

    EXPECT_EQ(high, 2);
    EXPECT_EQ(farLow, 2);
  }
}

TEST(CoinRushSample, EachLiftReachesItsIslandFromTheSide)
{
  LoadedTemplate project(templateFolder);
  ASSERT_TRUE(project.loaded);
  const auto& scene = *project.sceneManager.getCurrentScene();

  for (const char* side : { "North", "South" })
  {
    SCOPED_TRACE(side);
    const auto island = objectNamed(scene, std::string("Sky Island ") + side);
    const auto lift = objectNamed(scene, std::string("Lift ") + side);
    const float islandTop = positionOfJson(island).y + scaleOfJson(island).y;
    const float liftTop = positionOfJson(lift).y + scaleOfJson(lift).y
                          + vec3Of(fieldOf(scriptNamed(lift, "PingPongMover"), "m_offset")).y;

    EXPECT_TRUE(approx(islandTop, 6.0f));
    EXPECT_GE(liftTop, islandTop);
    EXPECT_LE(liftTop - islandTop, 0.6f);

    const float sign = positionOfJson(lift).z < 0 ? -1.0f : 1.0f;
    const float liftFar = positionOfJson(lift).z + sign * scaleOfJson(lift).z;
    const float islandNear = positionOfJson(island).z - sign * scaleOfJson(island).z;
    EXPECT_TRUE(approx(liftFar, islandNear));
  }
}

TEST(CoinRushSample, LiftsCarryRidersAndPushersShoveThem)
{
  LoadedTemplate project(templateFolder);
  ASSERT_TRUE(project.loaded);
  const auto& scene = *project.sceneManager.getCurrentScene();
  const auto roots = rootNames(scene);

  const auto expectMover = [&](const std::string& name, const glm::vec3& offset, const float speed,
                               const float pause, const bool carry) {
    SCOPED_TRACE(name);
    EXPECT_TRUE(roots.contains(name));
    const auto mover = objectNamed(scene, name);
    const auto script = scriptNamed(mover, "PingPongMover");
    EXPECT_EQ(vec3Of(fieldOf(script, "m_offset")), offset);
    EXPECT_EQ(fieldOf(script, "m_speed").get<float>(), speed);
    EXPECT_EQ(fieldOf(script, "m_pauseSeconds").get<float>(), pause);
    EXPECT_EQ(fieldOf(script, "m_carryRiders").get<bool>(), carry);
    EXPECT_EQ(textureOf(mover), "8925b024-c923-4839-9e6b-fa67e69e6752");
    EXPECT_FALSE(hasComponent(mover, "RigidBody"));
  };

  expectMover("Lift North", { 0, 6, 0 }, 2.0f, 2.0f, true);
  expectMover("Lift South", { 0, 6, 0 }, 2.0f, 2.0f, true);
  expectMover("Pusher 1", { 0, 0, 10 }, 3.0f, 0.5f, false);
  expectMover("Pusher 2", { 0, 0, -10 }, 3.0f, 0.5f, false);
}

TEST(CoinRushSample, PushersNeverEnterTheDaisOrARamp)
{
  LoadedTemplate project(templateFolder);
  ASSERT_TRUE(project.loaded);
  const auto& scene = *project.sceneManager.getCurrentScene();

  // Footprints on the ground plane as (minX, maxX, minZ, maxZ).
  using Box = std::array<float, 4>;
  std::vector<Box> blocked = { { -6, 6, -6, 6 } };
  for (const auto& ramp : objectsStartingWith(scene, "Ramp "))
  {
    const auto vertices = worldHullVertices(ramp);
    const auto xs = std::ranges::minmax(vertices, {}, &glm::vec3::x);
    const auto zs = std::ranges::minmax(vertices, {}, &glm::vec3::z);
    blocked.push_back({ xs.min.x, xs.max.x, zs.min.z, zs.max.z });
  }

  const auto overlaps = [](const Box& a, const Box& b) {
    return a[0] < b[1] - 1e-4f && b[0] < a[1] - 1e-4f && a[2] < b[3] - 1e-4f && b[2] < a[3] - 1e-4f;
  };

  for (const char* name : { "Pusher 1", "Pusher 2" })
  {
    const auto pusher = objectNamed(scene, name);
    const auto position = positionOfJson(pusher);
    const auto scale = scaleOfJson(pusher);
    const float travel = vec3Of(fieldOf(scriptNamed(pusher, "PingPongMover"), "m_offset")).z;
    const float minZ = std::min(position.z, position.z + travel) - scale.z;
    const float maxZ = std::max(position.z, position.z + travel) + scale.z;
    const Box swept = { position.x - scale.x, position.x + scale.x, minZ, maxZ };

    for (const auto& box : blocked)
    {
      EXPECT_FALSE(overlaps(swept, box)) << name;
    }

    // Positive control: the same footprint moved onto the dais does overlap it.
    const Box onDais = { -scale.x, scale.x, minZ, maxZ };
    EXPECT_TRUE(overlaps(onDais, blocked.front()));
  }
}

TEST(CoinRushSample, EverySpawnerNamesARegisteredPrefabWithItsSettings)
{
  LoadedTemplate project(templateFolder);
  ASSERT_TRUE(project.loaded);
  const auto& scene = *project.sceneManager.getCurrentScene();

  struct Expected { const char* object; const char* prefab; float interval; int maxAlive; float radius; };
  const std::vector<Expected> expected = {
    { "Boulder Spawner", "Big Boulder", 8.0f, 3, 2.0f }, { "Coin Spawner", "Coin", 1.5f, 12, 20.0f },
    { "Island Spawner North", "Big Coin", 15.0f, 3, 3.0f }, { "Island Spawner South", "Big Coin", 15.0f, 3, 3.0f }
  };

  for (const auto& row : expected)
  {
    SCOPED_TRACE(row.object);
    EXPECT_TRUE(rootNames(scene).contains(row.object));
    const auto script = scriptNamed(objectNamed(scene, row.object), "Spawner");
    const auto body = prefabBody(project, fieldOf(script, "m_prefab").get<std::string>());

    ASSERT_TRUE(body.is_object());
    EXPECT_EQ(body.at("name"), row.prefab);
    EXPECT_EQ(fieldOf(script, "m_interval").get<float>(), row.interval);
    EXPECT_EQ(fieldOf(script, "m_maxAlive").get<int>(), row.maxAlive);
    EXPECT_EQ(fieldOf(script, "m_offsetRadius").get<float>(), row.radius);
  }
}

TEST(CoinRushSample, BoulderSpawnerThrowsHorizontallyAndClearsTheDais)
{
  LoadedTemplate project(templateFolder);
  ASSERT_TRUE(project.loaded);
  const auto& scene = *project.sceneManager.getCurrentScene();
  const auto spawner = objectNamed(scene, "Boulder Spawner");
  const auto velocity = vec3Of(fieldOf(scriptNamed(spawner, "Spawner"), "m_initialVelocity"));

  EXPECT_EQ(velocity.y, 0.0f);
  EXPECT_GT(std::abs(velocity.x), 0.0f);
  EXPECT_GT(std::abs(velocity.z), 0.0f);

  // The hull's farthest vertex at the prefab's scale bounds how low a boulder can hang below its center.
  const auto body = prefabNamed(projectJson(templateFolder), "Big Boulder").at("body");
  float reach = 0.0f;
  for (const auto& vertex : worldHullVertices(body))
  {
    reach = std::max(reach, glm::length(vertex));
  }

  EXPECT_GT(reach, 1.0f);
  EXPECT_GT(positionOfJson(spawner).y - reach, 2.0f);
}

TEST(CoinRushSample, CoinsAreUprightTriggerRingsWithNoRigidBody)
{
  LoadedTemplate project(templateFolder);
  ASSERT_TRUE(project.loaded);
  const auto json = projectJson(templateFolder);

  struct Coin { const char* name; float scale; int value; };
  const std::vector<Coin> coins = { { "Coin", 0.8f, 1 }, { "Big Coin", 1.3f, 3 } };

  for (const auto& [name, scale, value] : coins)
  {
    SCOPED_TRACE(name);
    const auto body = prefabNamed(json, name).at("body");
    const auto transform = componentOf(body, "Transform");
    const auto collider = componentOf(body, "Collider", "Sphere");

    EXPECT_EQ(scaleOfJson(body), glm::vec3(scale));
    EXPECT_EQ(vec3Of(transform.at("rotation")), glm::vec3(90, 0, 0));
    EXPECT_EQ(componentOf(body, "ModelRenderer").at("modelUUID"), torusModel);
    EXPECT_EQ(textureOf(body), paletteYellowTexture);
    EXPECT_TRUE(collider.at("isTrigger").get<bool>());
    EXPECT_EQ(collider.at("radius").get<float>(), 1.0f);
    EXPECT_FALSE(hasComponent(body, "RigidBody"));
    EXPECT_EQ(fieldOf(scriptNamed(body, "Collectible"), "m_value").get<int>(), value);
    EXPECT_EQ(fieldOf(scriptNamed(body, "Spinner"), "m_degreesPerSecond").get<float>(), 90.0f);
    EXPECT_EQ(fieldOf(scriptNamed(body, "Bobber"), "m_amplitude").get<float>(), 0.25f);
    EXPECT_EQ(fieldOf(scriptNamed(body, "Bobber"), "m_frequencyHz").get<float>(), 0.5f);
  }
}

TEST(CoinRushSample, BigBoulderMassIsTheScaledVolumeOfTheBuiltInBouldersHull)
{
  const auto body = prefabNamed(projectJson(templateFolder), "Big Boulder").at("body");
  const auto collider = componentOf(body, "Collider", "ConvexMesh");

  ConvexHull hull;
  const auto flat = collider.at("vertices");
  for (std::size_t i = 0; i + 2 < flat.size(); i += 3)
  {
    hull.vertices.emplace_back(flat[i].get<float>(), flat[i + 1].get<float>(), flat[i + 2].get<float>());
  }

  hull.indices = collider.at("indices").get<std::vector<std::uint32_t>>();
  const float volume = hullVolume(hull);
  const float reference = hullVolume(boulderHull());

  EXPECT_EQ(scaleOfJson(body), glm::vec3(2.0f));
  EXPECT_EQ(hull.vertices.size(), 24u);
  EXPECT_GT(volume, 0.5f);
  EXPECT_NEAR(volume, reference, reference * 1e-3f);
  EXPECT_NEAR(componentOf(body, "RigidBody").at("mass").get<float>(), reference * 8.0f,
              reference * 8.0f * 1e-3f);
  EXPECT_NEAR(componentOf(body, "RigidBody").at("mass").get<float>(), 11.56f, 0.1f);
}

TEST(CoinRushSample, SceneScriptsCarryTheRequiredFields)
{
  LoadedTemplate project(templateFolder);
  ASSERT_TRUE(project.loaded);
  const auto scene = projectJson(templateFolder).at("assets").at("scenes").at(0);
  const auto& scripts = scene.at("scripts");

  ASSERT_EQ(scripts.size(), 3u);
  EXPECT_EQ(fieldOf(scriptNamed(scene, "PlayerSpawnManager"), "m_tintPlayers").get<bool>(), true);
  EXPECT_EQ(fieldOf(scriptNamed(scene, "PlayerSpawnManager"), "m_respawnBelowY").get<float>(), -20.0f);
  EXPECT_EQ(fieldOf(scriptNamed(scene, "ScoreKeeper"), "m_targetScore").get<int>(), 0);
  EXPECT_EQ(fieldOf(scriptNamed(scene, "ScoreKeeper"), "m_resetOnRoundOver").get<bool>(), true);
  EXPECT_EQ(fieldOf(scriptNamed(scene, "RoundTimer"), "m_roundSeconds").get<int>(), 90);
  EXPECT_EQ(fieldOf(scriptNamed(scene, "RoundTimer"), "m_intermissionSeconds").get<float>(), 5.0f);

  const auto& live = project.sceneManager.getCurrentScene()->getScripts();
  ASSERT_EQ(live.size(), 3u);
  EXPECT_EQ(live.front()->getClassName(), "PlayerSpawnManager");
}

TEST(CoinRushSample, PlayerPrefabIsRegisteredForTheSpawnManager)
{
  LoadedTemplate project(templateFolder);
  ASSERT_TRUE(project.loaded);

  const auto body = prefabBody(project, playerPrefab);
  ASSERT_TRUE(body.is_object());
  EXPECT_EQ(body.at("name"), "Player");
  EXPECT_TRUE(hasComponent(body, "PlayerController"));
  EXPECT_TRUE(hasComponent(body, "Camera"));
  EXPECT_EQ(scriptNamed(body, "PlayerScript").at("className"), "PlayerScript");

  // Positive control: an unregistered uuid does not resolve.
  EXPECT_TRUE(prefabBody(project, gridCheckerTexture).is_null());
}

TEST(CoinRushSample, EveryReferencedAssetIsRegisteredAndItsFileExists)
{
  const auto project = projectJson(templateFolder);
  const auto assets = sourceRoot / "apps" / "server" / "defaultAssets";
  std::set<std::string> registered;

  for (const char* list : { "models", "textures" })
  {
    for (const auto& asset : project.at("assets").at(list))
    {
      registered.insert(asset.at("uuid").get<std::string>());
      const auto filePath = asset.at("filePath").get<std::string>();
      EXPECT_TRUE(std::filesystem::is_regular_file(assets / filePath.substr(7))) << filePath;
    }
  }

  std::vector<std::string> references;
  for (const auto& object : project.at("assets").at("scenes").at(0).at("objects"))
  {
    collectReferences(object, references);
  }

  for (const auto& prefab : project.at("assets").at("prefabs"))
  {
    collectReferences(prefab.at("body"), references);
  }

  EXPECT_GT(references.size(), 40u);
  for (const auto& reference : references)
  {
    EXPECT_TRUE(registered.contains(reference)) << reference;
  }

  for (const auto& uuid : registered)
  {
    EXPECT_NE(std::ranges::count(references, uuid), 0) << uuid;
  }
}

TEST(CoinRushSample, ScriptsFolderHoldsOnlyScorePillarAndItsReadme)
{
  const auto scripts = templateFolder / "scripts";
  std::set<std::string> files;
  for (const auto& entry : std::filesystem::directory_iterator(scripts))
  {
    files.insert(entry.path().filename().string());
  }

  EXPECT_EQ(files, (std::set<std::string>{ "README.md", "ScorePillar.cs" }));
}

TEST(CoinRushSample, PlayersBoulderAndDaisSettleWithoutFallingThrough)
{
  LoadedTemplate project(templateFolder);
  ASSERT_TRUE(project.loaded);
  auto& objectManager = *project.sceneManager.getCurrentScene()->getObjectManager();
  const auto playerBody = prefabBody(project, playerPrefab);
  const auto boulderBody = prefabNamed(projectJson(templateFolder), "Big Boulder").at("body");

  const auto place = [&](const nlohmann::json& body, const glm::vec3& position) {
    const auto object = objectManager.instantiate(body);
    fixtures::transformOf(object)->setPosition(position);
    return object;
  };

  const auto atSpawn = place(playerBody, { -24, 1.02f, -24 });
  const auto onDais = place(playerBody, { 0, 3.02f, 0 });
  const auto boulder = place(boulderBody, { 3, 7, 3 });

  CollisionSystem collisionSystem;
  for (int tick = 0; tick < 300; ++tick)
  {
    PhysicsSystem::fixedUpdate(objectManager, dt);
    collisionSystem.fixedUpdate(objectManager, dt);
  }

  EXPECT_NEAR(fixtures::positionOf(atSpawn).y, 1.0f, 0.1f);
  EXPECT_NEAR(fixtures::positionOf(onDais).y, 3.0f, 0.1f);

  const auto boulderPosition = fixtures::positionOf(boulder);
  EXPECT_TRUE(std::isfinite(boulderPosition.x) && std::isfinite(boulderPosition.y) && std::isfinite(boulderPosition.z));
  EXPECT_GT(boulderPosition.y, 0.5f);
}

TEST(CoinRushSample, AThrownBoulderLeavesTheDaisFootprint)
{
  LoadedTemplate project(templateFolder);
  ASSERT_TRUE(project.loaded);
  auto& objectManager = *project.sceneManager.getCurrentScene()->getObjectManager();
  const auto& scene = *project.sceneManager.getCurrentScene();
  const auto velocity = vec3Of(fieldOf(scriptNamed(objectNamed(scene, "Boulder Spawner"), "Spawner"),
                                       "m_initialVelocity"));

  const auto boulder = objectManager.instantiate(prefabNamed(projectJson(templateFolder), "Big Boulder").at("body"));
  fixtures::transformOf(boulder)->setPosition({ 1, 7, -1 });
  boulder->getComponent<RigidBody>(ComponentType::rigidBody)->setVelocity(velocity * dt);

  CollisionSystem collisionSystem;
  bool left = false;
  for (int tick = 0; tick < 300 && !left; ++tick)
  {
    PhysicsSystem::fixedUpdate(objectManager, dt);
    collisionSystem.fixedUpdate(objectManager, dt);
    const auto position = fixtures::positionOf(boulder);
    left = std::max(std::abs(position.x), std::abs(position.z)) > 6.0f;
  }

  EXPECT_TRUE(left);
  EXPECT_GT(fixtures::positionOf(boulder).y, 0.5f);
}

TEST(CoinRushSample, ThumbnailIs640By360)
{
  std::ifstream file(templateFolder / "thumbnail.png", std::ios::binary);
  ASSERT_TRUE(file.is_open());

  unsigned char header[24] = {};
  file.read(reinterpret_cast<char*>(header), sizeof(header));
  ASSERT_EQ(file.gcount(), 24);
  EXPECT_EQ(std::string(reinterpret_cast<char*>(header) + 12, 4), "IHDR");

  const auto bigEndian = [&](const int offset) {
    return (static_cast<std::uint32_t>(header[offset]) << 24) | (static_cast<std::uint32_t>(header[offset + 1]) << 16)
         | (static_cast<std::uint32_t>(header[offset + 2]) << 8) | static_cast<std::uint32_t>(header[offset + 3]);
  };

  EXPECT_EQ(bigEndian(16), 640u);
  EXPECT_EQ(bigEndian(20), 360u);
}
