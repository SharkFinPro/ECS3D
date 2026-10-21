#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "DefaultShapes.h"
#include "PhysicsSystem.h"
#include "CoinRushTestHelpers.h"
#include "TemplateTestHelpers.h"
#include "TestScene.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/Script.h"
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
#include <numbers>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace templateTest;
using namespace coinrushTest;


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

    // Seen from above the lift sits wholly on the island's width, and it rests on the floor at its low point.
    const float liftMin = positionOfJson(lift).x - scaleOfJson(lift).x;
    const float liftMax = positionOfJson(lift).x + scaleOfJson(lift).x;
    EXPECT_GE(liftMin, positionOfJson(island).x - scaleOfJson(island).x);
    EXPECT_LE(liftMax, positionOfJson(island).x + scaleOfJson(island).x);
    EXPECT_TRUE(approx(positionOfJson(lift).y - scaleOfJson(lift).y, 0.0f));
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

  const auto body = prefabNamed(projectJson(templateFolder), "Big Boulder").at("body");
  const float radius = boulderWorldRadius(body);

  EXPECT_GT(radius, 1.0f);
  EXPECT_GT(positionOfJson(spawner).y - radius - 1.0f, 2.0f);
}

TEST(CoinRushSample, BigBoulderIsADensityOneBallOfTheScaledHullVolume)
{
  const auto body = prefabNamed(projectJson(templateFolder), "Big Boulder").at("body");
  const float reference = hullVolume(boulderHull());
  const float mass = componentOf(body, "RigidBody").at("mass").get<float>();
  const float radius = boulderWorldRadius(body);
  const float ballVolume = 4.0f / 3.0f * std::numbers::pi_v<float> * radius * radius * radius;

  EXPECT_EQ(scaleOfJson(body), glm::vec3(2.0f));
  EXPECT_EQ(componentOf(body, "ModelRenderer").at("modelUUID"), boulderModel);
  EXPECT_FALSE(componentOf(body, "Collider", "Sphere").at("isTrigger").get<bool>());
  EXPECT_NEAR(mass, reference * 8.0f, reference * 8.0f * 1e-3f);
  EXPECT_NEAR(mass, 11.56f, 0.1f);
  EXPECT_NEAR(ballVolume, mass, mass * 1e-3f);
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
