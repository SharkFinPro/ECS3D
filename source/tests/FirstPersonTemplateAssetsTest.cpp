#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "DefaultShapes.h"
#include "PhysicsSystem.h"
#include "ProjectSerializer.h"
#include "DefaultProject.h"
#include "TemplateTestHelpers.h"
#include "TestScene.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/LightRenderer.h"
#include "objects/components/RigidBody.h"
#include "objects/components/RotationConvention.h"
#include "objects/components/Script.h"
#include "objects/components/Transform.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"
#include "templates/TemplateManifest.h"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <uuid.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <utility>
#include <set>
#include <string>
#include <vector>

using namespace templateTest;

namespace
{
  const std::filesystem::path templateFolder = sourceRoot / "templates" / "first-person";

  constexpr const char* gridCheckerTexture = "48ad67dd-957f-4fc9-bd7b-019debffa1d0";
  constexpr const char* rampPrefab = "1b112469-4331-402e-bc4a-94cd0368ef52";

  constexpr float dt = 1.0f / 50.0f;

  // The default Player Prefab the PlayerSpawnManager script spawns, read from its source.
  std::string spawnManagerDefaultPrefab()
  {
    const auto text = readText(sourceRoot / "libs" / "scripting" / "UserScripts" / "Starters" / "PlayerSpawnManager.cs");
    const std::string marker = "m_playerPrefab = \"";
    const auto begin = text.find(marker);
    if (begin == std::string::npos)
    {
      ADD_FAILURE() << "PlayerSpawnManager.cs no longer declares m_playerPrefab";
      return {};
    }

    const auto start = begin + marker.size();
    return text.substr(start, text.find('"', start) - start);
  }

  // The body after a load, so keys the loader defaults compare alike; the uuid and a hull's triangulation
  // (which differs by how it was built) are dropped.
  nlohmann::json normalizedBody(const nlohmann::json& body)
  {
    const auto componentRegistry = std::make_shared<ComponentRegistry>();
    registerDataComponents(*componentRegistry);
    ObjectManager scratch(componentRegistry);

    auto loaded = scratch.instantiate(body)->serialize();
    loaded.erase("uuid");
    for (auto& component : loaded.at("components"))
    {
      if (component.value("subType", std::string()) == "ConvexMesh")
      {
        component.erase("vertices");
        component.erase("indices");
      }
    }

    std::ranges::sort(loaded.at("components"), {}, [](const nlohmann::json& component) {
      return component.at("type").get<std::string>() + component.value("subType", std::string());
    });
    return loaded;
  }

  nlohmann::json prefabBodyNamed(const nlohmann::json& project, const std::string& name)
  {
    for (const auto& prefab : project.at("assets").at("prefabs"))
    {
      if (prefab.at("name") == name)
      {
        return prefab.at("body");
      }
    }

    ADD_FAILURE() << "no prefab named " << name;
    return nlohmann::json::object();
  }
}

TEST(FirstPersonTemplate, TheRampsHullIsTheBuiltInRampsHull)
{
  const auto project = projectJson(templateFolder);
  const auto reference = rampHull();
  ASSERT_FALSE(reference.vertices.empty());

  for (const auto& prefab : project.at("assets").at("prefabs"))
  {
    if (prefab.at("name") != "Ramp")
    {
      continue;
    }

    const auto collider = componentOf(prefab.at("body"), "Collider", "ConvexMesh");
    const auto& flat = collider.at("vertices");
    ASSERT_EQ(flat.size(), reference.vertices.size() * 3);

    std::vector<glm::vec3> vertices;
    for (std::size_t i = 0; i < flat.size(); i += 3)
    {
      vertices.emplace_back(flat[i].get<float>(), flat[i + 1].get<float>(), flat[i + 2].get<float>());
    }

    for (const auto& expected : reference.vertices)
    {
      EXPECT_EQ(std::ranges::count(vertices, expected), 1) << expected.x << ", " << expected.y << ", " << expected.z;
    }

    const auto indices = collider.at("indices").get<std::vector<std::uint32_t>>();
    EXPECT_EQ(indices.size(), reference.indices.size());

    // Counter-clockwise from outside: every triangle's normal points away from the hull's center.
    glm::vec3 center(0);
    for (const auto& vertex : vertices)
    {
      center += vertex / static_cast<float>(vertices.size());
    }

    for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
    {
      const auto& p0 = vertices.at(indices[i]);
      const auto normal = glm::cross(vertices.at(indices[i + 1]) - p0, vertices.at(indices[i + 2]) - p0);
      EXPECT_GT(glm::dot(normal, p0 - center), 0.0f) << "triangle " << i / 3;
    }

    return;
  }

  FAIL() << "no Ramp prefab";
}

TEST(FirstPersonTemplate, TheSceneHoldsPlayerSpawnManagerAndNoPlacedPlayer)
{
  LoadedTemplate project(templateFolder);
  ASSERT_TRUE(project.loaded);
  const auto& scene = *project.sceneManager.getCurrentScene();

  ASSERT_EQ(scene.getScripts().size(), 1u);
  EXPECT_EQ(scene.getScripts().front()->getClassName(), "PlayerSpawnManager");

  // Positive control: the walk covers the objects the template is known to hold.
  std::set<std::string> names;
  for (const auto& object : scene.getObjectManager()->getAllObjects())
  {
    names.insert(object->getName());
    EXPECT_EQ(object->getName().find("Player"), std::string::npos) << object->getName();
    EXPECT_FALSE(hasComponent(object->serialize(), "PlayerController")) << object->getName();
    EXPECT_FALSE(hasComponent(object->serialize(), "Camera")) << object->getName();
    EXPECT_TRUE(object->serialize().at("scripts").empty()) << object->getName();
  }

  for (const char* expected : { "Ground", "Sun", "Crate 1", "Crate 6", "Platform", "Ramp 1", "Ramp 2",
                                "SpawnPoint1", "SpawnPoint4" })
  {
    EXPECT_TRUE(names.contains(expected)) << expected;
  }
}

TEST(FirstPersonTemplate, EveryReferencedAssetAndPrefabIsRegistered)
{
  const auto project = projectJson(templateFolder);
  std::set<std::string> registered;
  for (const char* list : { "models", "textures", "prefabs" })
  {
    for (const auto& asset : project.at("assets").at(list))
    {
      registered.insert(asset.at("uuid").get<std::string>());
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

  // Ground and tiles, sun-less crates, platform, ramps, discs and two prefab bodies hold well over this many.
  EXPECT_GT(references.size(), 40u);
  for (const auto& reference : references)
  {
    EXPECT_TRUE(registered.contains(reference)) << reference;
  }

  // Nothing registered is left unused except the prefabs, which scripts spawn.
  for (const char* list : { "models", "textures" })
  {
    for (const auto& asset : project.at("assets").at(list))
    {
      EXPECT_NE(std::ranges::count(references, asset.at("uuid").get<std::string>()), 0) << asset.at("filePath");
    }
  }
}

TEST(FirstPersonTemplate, ThePlayerPrefabPlayerSpawnManagerSpawnsResolvesInTheLoadedRegistry)
{
  LoadedTemplate project(templateFolder);
  ASSERT_TRUE(project.loaded);

  const auto playerPrefab = spawnManagerDefaultPrefab();
  ASSERT_FALSE(playerPrefab.empty());

  const auto uuid = uuids::uuid::from_string(playerPrefab);
  ASSERT_TRUE(uuid.has_value());

  const auto body = project.assetRegistry.getPrefabBody(*uuid);
  ASSERT_TRUE(body.is_object());
  EXPECT_EQ(body.at("name"), "Player");
  EXPECT_TRUE(hasComponent(body, "PlayerController"));
  EXPECT_TRUE(hasComponent(body, "Camera"));
  EXPECT_TRUE(hasComponent(body, "RigidBody"));

  ASSERT_EQ(body.at("scripts").size(), 1u);
  EXPECT_EQ(body.at("scripts").at(0).at("className"), "PlayerScript");

  const auto ramp = project.assetRegistry.getPrefabBody(uuids::uuid::from_string(rampPrefab).value());
  ASSERT_TRUE(ramp.is_object());
  EXPECT_EQ(ramp.at("name"), "Ramp");
  EXPECT_FALSE(hasComponent(ramp, "RigidBody"));

  // Positive control: an unregistered uuid does not resolve.
  EXPECT_TRUE(project.assetRegistry.getPrefabBody(uuids::uuid::from_string(gridCheckerTexture).value()).is_null());
}

TEST(FirstPersonTemplate, EveryRegisteredAssetFileExistsAmongTheDefaultAssets)
{
  const auto project = projectJson(templateFolder);
  const auto assets = sourceRoot / "apps" / "server" / "defaultAssets";

  int checked = 0;
  for (const char* list : { "models", "textures" })
  {
    for (const auto& asset : project.at("assets").at(list))
    {
      const auto filePath = asset.at("filePath").get<std::string>();
      ASSERT_EQ(filePath.rfind("assets/", 0), 0u) << filePath;
      EXPECT_TRUE(std::filesystem::is_regular_file(assets / filePath.substr(7))) << filePath;
      ++checked;
    }
  }

  EXPECT_EQ(checked, 10);
}

TEST(FirstPersonTemplate, ScriptsFolderHoldsOnlyItsReadmeAndCopiesNoBuiltInScript)
{
  const auto scripts = templateFolder / "scripts";
  EXPECT_TRUE(std::filesystem::is_regular_file(scripts / "README.md"));

  int entries = 0;
  for (const auto& entry : std::filesystem::directory_iterator(scripts))
  {
    ++entries;
    EXPECT_NE(entry.path().extension(), ".cs") << entry.path().string();
  }

  EXPECT_EQ(entries, 1);
}

namespace
{
  struct Crate
  {
    std::shared_ptr<Object> object;
    glm::vec3 start;
  };

  void expectFiniteAndAbove(const ObjectManager& objectManager)
  {
    for (const auto& object : objectManager.getAllObjects())
    {
      const auto position = fixtures::positionOf(object);
      EXPECT_TRUE(std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z))
        << object->getName();
      EXPECT_GT(position.y, -50.0f) << object->getName();
    }
  }

  void expectCrateAtRest(const Crate& crate)
  {
    SCOPED_TRACE(crate.object->getName());
    const auto position = fixtures::positionOf(crate.object);
    EXPECT_LT(glm::distance(position, crate.start), 0.05f);

    const auto body = crate.object->getComponent<RigidBody>(ComponentType::rigidBody);
    EXPECT_LT(glm::length(body->getVelocity()), 0.05f);
    EXPECT_LT(glm::length(body->getAngularVelocity()), 1.0f);

    // The ground holds the bottom crates at their half height.
    if (crate.start.y < 1.0f)
    {
      EXPECT_NEAR(position.y, 0.75f, 0.05f);
    }

    const auto orientation = fixtures::transformOf(crate.object)->getOrientation();
    const float angle = glm::degrees(2.0f * std::acos(std::min(1.0f, std::abs(orientation.w))));
    EXPECT_LT(angle, 2.0f);
  }
}

TEST(FirstPersonTemplate, CratesStayPutAndNothingFallsOverThreeHundredTicks)
{
  LoadedTemplate project(templateFolder);
  ASSERT_TRUE(project.loaded);
  const auto& objectManager = *project.sceneManager.getCurrentScene()->getObjectManager();

  std::vector<Crate> crates;
  for (const auto& object : objectManager.getAllObjects())
  {
    if (object->getComponent<RigidBody>(ComponentType::rigidBody))
    {
      crates.push_back({ object, fixtures::positionOf(object) });
    }
  }

  // The crates are the positive control that physics ran at all.
  ASSERT_EQ(crates.size(), 6u);

  CollisionSystem collisionSystem;
  for (int tick = 0; tick < 300; ++tick)
  {
    PhysicsSystem::fixedUpdate(objectManager, dt);
    collisionSystem.fixedUpdate(objectManager, dt);
  }

  expectFiniteAndAbove(objectManager);
  for (const auto& crate : crates)
  {
    expectCrateAtRest(crate);
  }
}

TEST(FirstPersonTemplate, ThumbnailIs640By360)
{
  std::ifstream file(templateFolder / "thumbnail.png", std::ios::binary);
  ASSERT_TRUE(file.is_open());

  unsigned char header[24] = {};
  file.read(reinterpret_cast<char*>(header), sizeof(header));
  ASSERT_EQ(file.gcount(), 24);

  const unsigned char signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
  for (int i = 0; i < 8; ++i)
  {
    EXPECT_EQ(header[i], signature[i]);
  }

  EXPECT_EQ(std::string(reinterpret_cast<char*>(header) + 12, 4), "IHDR");

  const auto bigEndian = [&](const int offset)
  {
    return (static_cast<std::uint32_t>(header[offset]) << 24) | (static_cast<std::uint32_t>(header[offset + 1]) << 16)
         | (static_cast<std::uint32_t>(header[offset + 2]) << 8) | static_cast<std::uint32_t>(header[offset + 3]);
  };

  EXPECT_EQ(bigEndian(16), 640u);
  EXPECT_EQ(bigEndian(20), 360u);
}
