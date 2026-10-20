#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "DefaultShapes.h"
#include "PhysicsSystem.h"
#include "ProjectSerializer.h"
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

namespace
{
  const std::filesystem::path sourceRoot = std::filesystem::path(ECS3D_SOURCE_DIR) / "source";
  const std::filesystem::path templateFolder = sourceRoot / "templates" / "first-person";

  constexpr const char* planeTileModel = "6a742837-aa60-43fe-a652-32a3d385cde0";
  constexpr const char* gridCheckerTexture = "48ad67dd-957f-4fc9-bd7b-019debffa1d0";
  constexpr const char* checkerOrangeTexture = "425f3784-74eb-4e09-bfbb-88facfb014d0";
  constexpr const char* checkerBlueTexture = "05325ffd-75bd-4d76-a50d-7b80e183b081";
  constexpr const char* paletteYellowTexture = "1d5edebf-57f9-499c-9840-b84a57174422";
  constexpr const char* cylinderModel = "a383cc8f-ee6e-4373-ae06-f10f85ef159b";
  constexpr const char* rampModel = "e77194e0-e50e-4070-aacd-2f5d917b2da7";
  constexpr const char* rampPrefab = "1b112469-4331-402e-bc4a-94cd0368ef52";

  constexpr float dt = 1.0f / 50.0f;

  glm::vec3 vec3Of(const nlohmann::json& value)
  {
    return { value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>() };
  }

  bool hasComponent(const nlohmann::json& object, const std::string& type)
  {
    return std::ranges::any_of(object.at("components"),
                               [&](const nlohmann::json& component) { return component.at("type") == type; });
  }

  nlohmann::json componentOf(const nlohmann::json& object, const std::string& type, const std::string& subType = "")
  {
    for (const auto& component : object.at("components"))
    {
      if (component.at("type") == type && (subType.empty() || component.at("subType") == subType))
      {
        return component;
      }
    }

    ADD_FAILURE() << object.at("name") << " has no " << type;
    return nlohmann::json::object();
  }

  nlohmann::json projectJson()
  {
    std::ifstream file(templateFolder / "project.ecs3d");
    EXPECT_TRUE(file.is_open());
    return nlohmann::json::parse(file);
  }

  // The objects of the one scene, serialized, in file order.
  std::vector<nlohmann::json> sceneObjects(const SceneAsset& scene)
  {
    std::vector<nlohmann::json> objects;
    for (const auto& object : scene.getObjectManager()->getObjects())
    {
      objects.push_back(object->serialize());
    }

    return objects;
  }

  nlohmann::json objectNamed(const SceneAsset& scene, const std::string& name)
  {
    for (const auto& object : sceneObjects(scene))
    {
      if (object.at("name") == name)
      {
        return object;
      }
    }

    ADD_FAILURE() << "no object named " << name;
    return nlohmann::json::object();
  }

  std::vector<nlohmann::json> objectsStartingWith(const SceneAsset& scene, const std::string& prefix)
  {
    std::vector<nlohmann::json> matches;
    for (const auto& object : sceneObjects(scene))
    {
      if (object.at("name").get<std::string>().rfind(prefix, 0) == 0)
      {
        matches.push_back(object);
      }
    }

    return matches;
  }

  glm::vec3 positionOfJson(const nlohmann::json& object)
  {
    return vec3Of(componentOf(object, "Transform").at("position"));
  }

  glm::vec3 scaleOfJson(const nlohmann::json& object)
  {
    return vec3Of(componentOf(object, "Transform").at("scale"));
  }

  struct LoadedTemplate
  {
    std::shared_ptr<ComponentRegistry> componentRegistry = std::make_shared<ComponentRegistry>();
    AssetRegistry assetRegistry;
    SceneManager sceneManager;
    bool loaded = false;

    LoadedTemplate()
    {
      registerDataComponents(*componentRegistry);
      const ProjectSerializer serializer(&assetRegistry, &sceneManager, componentRegistry);
      loaded = serializer.load((templateFolder / "project.ecs3d").string());
    }
  };

  std::string readText(const std::filesystem::path& path)
  {
    std::ifstream file(path);
    EXPECT_TRUE(file.is_open()) << path.string();
    return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
  }

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

  void collectReferences(const nlohmann::json& object, std::vector<std::string>& references)
  {
    for (const auto& component : object.at("components"))
    {
      if (component.at("type") == "ModelRenderer")
      {
        for (const char* key : { "modelUUID", "textureUUID", "specularMapUUID" })
        {
          references.push_back(component.at(key).get<std::string>());
        }
      }
      else if (component.at("type") == "Collider" && component.contains("sourceModel"))
      {
        references.push_back(component.at("sourceModel").get<std::string>());
      }
    }

    for (const auto& child : object.at("children"))
    {
      collectReferences(child, references);
    }
  }

  std::vector<glm::vec3> worldVertices(const nlohmann::json& rampObject)
  {
    const auto transform = componentOf(rampObject, "Transform");
    const auto position = vec3Of(transform.at("position"));
    const auto scale = vec3Of(transform.at("scale"));
    const auto orientation = eulerDegreesToQuat(vec3Of(transform.at("rotation")));

    const auto collider = componentOf(rampObject, "Collider", "ConvexMesh");
    const auto& flat = collider.at("vertices");

    std::vector<glm::vec3> vertices;
    for (std::size_t i = 0; i < flat.size(); i += 3)
    {
      const glm::vec3 local(flat[i].get<float>(), flat[i + 1].get<float>(), flat[i + 2].get<float>());
      vertices.push_back(position + orientation * (scale * local));
    }

    return vertices;
  }

  bool approx(const float a, const float b)
  {
    return std::abs(a - b) < 1e-4f;
  }
}

TEST(FirstPersonTemplate, ManifestParsesWithTheShippedFields)
{
  std::string error;
  const auto manifest = loadTemplateManifest(templateFolder, &error);
  ASSERT_TRUE(manifest.has_value()) << error;

  EXPECT_EQ(manifest->id, "first-person");
  EXPECT_EQ(manifest->name, "First-Person");
  EXPECT_EQ(manifest->category, "template");
  EXPECT_EQ(manifest->tags, (std::vector<std::string>{ "starter", "multiplayer" }));
  EXPECT_EQ(manifest->thumbnail, "thumbnail.png");
  EXPECT_EQ(manifest->entryScene, "Main");
  EXPECT_EQ(manifest->minEngineVersion, "0.0.1");
  EXPECT_FALSE(manifest->description.empty());

  ASSERT_TRUE(manifest->players.has_value());
  EXPECT_EQ(manifest->players->min, 1);
  EXPECT_EQ(manifest->players->max, 4);
}

TEST(FirstPersonTemplate, ProjectLoadsWithMainAsTheCurrentScene)
{
  LoadedTemplate project;
  ASSERT_TRUE(project.loaded);

  const auto scene = project.sceneManager.getCurrentScene();
  ASSERT_NE(scene, nullptr);
  EXPECT_EQ(scene->getName(), "Main");
  EXPECT_EQ(project.sceneManager.getScenes().size(), 1u);
}

TEST(FirstPersonTemplate, GroundIsFortyByFortyOfFourGridTiles)
{
  LoadedTemplate project;
  ASSERT_TRUE(project.loaded);
  const auto ground = objectNamed(*project.sceneManager.getCurrentScene(), "Ground");

  EXPECT_EQ(positionOfJson(ground), glm::vec3(0, -1, 0));
  EXPECT_EQ(scaleOfJson(ground), glm::vec3(1));
  EXPECT_EQ(vec3Of(componentOf(ground, "Collider", "Box").at("scale")), glm::vec3(20, 1, 20));
  EXPECT_FALSE(componentOf(ground, "ModelRenderer").at("shouldRender").get<bool>());
  EXPECT_FALSE(hasComponent(ground, "RigidBody"));

  ASSERT_EQ(ground.at("children").size(), 4u);
  std::set<std::pair<float, float>> corners;
  for (const auto& tile : ground.at("children"))
  {
    EXPECT_EQ(scaleOfJson(tile), glm::vec3(10, 1, 10));
    EXPECT_EQ(positionOfJson(tile).y, 1.0f);
    EXPECT_FALSE(hasComponent(tile, "Collider"));

    const auto renderer = componentOf(tile, "ModelRenderer");
    EXPECT_TRUE(renderer.at("shouldRender").get<bool>());
    EXPECT_EQ(renderer.at("modelUUID"), planeTileModel);
    EXPECT_EQ(renderer.at("textureUUID"), gridCheckerTexture);

    corners.emplace(positionOfJson(tile).x, positionOfJson(tile).z);
  }

  const std::set<std::pair<float, float>> expected = { { -10, -10 }, { 10, -10 }, { -10, 10 }, { 10, 10 } };
  EXPECT_EQ(corners, expected);
}

TEST(FirstPersonTemplate, HasExactlyOneSunFromTheLightingRig)
{
  LoadedTemplate project;
  ASSERT_TRUE(project.loaded);
  const auto& scene = *project.sceneManager.getCurrentScene();

  int lights = 0;
  for (const auto& object : scene.getObjectManager()->getAllObjects())
  {
    lights += object->getComponent<LightRenderer>(ComponentType::lightRenderer) ? 1 : 0;
  }

  EXPECT_EQ(lights, 1);

  const auto sun = objectNamed(scene, "Sun");
  EXPECT_EQ(positionOfJson(sun), glm::vec3(10, 25, 10));

  const auto light = componentOf(sun, "LightRenderer");
  EXPECT_FALSE(light.at("isSpotlight").get<bool>());
  EXPECT_NEAR(glm::distance(vec3Of(light.at("color")), glm::vec3(1.0f, 0.97f, 0.9f)), 0.0f, 1e-5f);
  EXPECT_NEAR(light.at("ambient").get<float>(), 0.25f, 1e-6f);
  EXPECT_NEAR(light.at("diffuse").get<float>(), 0.8f, 1e-6f);
  EXPECT_NEAR(light.at("specular").get<float>(), 0.5f, 1e-6f);
}

TEST(FirstPersonTemplate, SixCratesWeighTheirVolumeAndStackExactlyTouching)
{
  LoadedTemplate project;
  ASSERT_TRUE(project.loaded);
  const auto crates = objectsStartingWith(*project.sceneManager.getCurrentScene(), "Crate ");
  ASSERT_EQ(crates.size(), 6u);

  std::vector<glm::vec3> positions;
  for (const auto& crate : crates)
  {
    EXPECT_EQ(scaleOfJson(crate), glm::vec3(0.75f));
    EXPECT_EQ(componentOf(crate, "ModelRenderer").at("textureUUID"), checkerOrangeTexture);
    EXPECT_FALSE(hasComponent(crate, "Script"));

    // A Block's mass is 8 x scale^3: its volume at density one.
    EXPECT_NEAR(componentOf(crate, "RigidBody").at("mass").get<float>(), 8.0f * 0.75f * 0.75f * 0.75f, 1e-5f);
    EXPECT_EQ(vec3Of(componentOf(crate, "Collider", "Box").at("scale")), glm::vec3(1));
    positions.push_back(positionOfJson(crate));
  }

  int stacks = 0;
  int singles = 0;
  for (const auto& position : positions)
  {
    if (position.y == 0.75f)
    {
      const auto above = std::ranges::count_if(positions, [&](const glm::vec3& other) {
        return other.x == position.x && other.z == position.z && other.y == 2.25f;
      });
      stacks += above == 1 ? 1 : 0;
      singles += above == 0 ? 1 : 0;
    }
    else
    {
      // A top crate has a crate exactly one body height (1.5) below it, and nothing else stands off the floor.
      EXPECT_EQ(position.y, 2.25f);
    }
  }

  EXPECT_EQ(stacks, 2);
  EXPECT_EQ(singles, 2);
}

TEST(FirstPersonTemplate, TwoRampsMeetTheTopEdgeOfTheStaticPlatform)
{
  LoadedTemplate project;
  ASSERT_TRUE(project.loaded);
  const auto& scene = *project.sceneManager.getCurrentScene();

  const auto platform = objectNamed(scene, "Platform");
  EXPECT_EQ(positionOfJson(platform), glm::vec3(10, 1, -8));
  EXPECT_EQ(scaleOfJson(platform), glm::vec3(2, 1, 2));
  EXPECT_EQ(componentOf(platform, "ModelRenderer").at("textureUUID"), checkerBlueTexture);
  EXPECT_FALSE(hasComponent(platform, "RigidBody"));
  EXPECT_TRUE(hasComponent(platform, "Collider"));

  const auto ramps = objectsStartingWith(scene, "Ramp ");
  ASSERT_EQ(ramps.size(), 2u);

  // The platform spans x 8..12, y 0..2, z -10..-6. The ramp model's high end is its local +X top edge.
  bool reachesWestFace = false;
  bool reachesSouthFace = false;
  for (const auto& ramp : ramps)
  {
    EXPECT_EQ(scaleOfJson(ramp), glm::vec3(2, 1, 2));
    EXPECT_FALSE(hasComponent(ramp, "RigidBody"));
    EXPECT_EQ(componentOf(ramp, "ModelRenderer").at("modelUUID"), rampModel);

    const auto vertices = worldVertices(ramp);
    ASSERT_EQ(vertices.size(), 6u);

    // The low end rests on the ground and the high end reaches the platform's top.
    float lowest = vertices.front().y;
    float highest = vertices.front().y;
    for (const auto& vertex : vertices)
    {
      lowest = std::min(lowest, vertex.y);
      highest = std::max(highest, vertex.y);
    }

    EXPECT_TRUE(approx(lowest, 0.0f));
    EXPECT_TRUE(approx(highest, 2.0f));

    std::vector<glm::vec3> highEdge;
    for (const auto& vertex : vertices)
    {
      if (approx(vertex.y, 2.0f))
      {
        highEdge.push_back(vertex);
      }
    }

    ASSERT_EQ(highEdge.size(), 2u);
    const bool onWestFace = approx(highEdge[0].x, 8.0f) && approx(highEdge[1].x, 8.0f);
    const bool onSouthFace = approx(highEdge[0].z, -6.0f) && approx(highEdge[1].z, -6.0f);
    reachesWestFace = reachesWestFace || onWestFace;
    reachesSouthFace = reachesSouthFace || onSouthFace;

    // The edge lies within the face it touches.
    for (const auto& vertex : highEdge)
    {
      EXPECT_GE(vertex.x, 8.0f - 1e-4f);
      EXPECT_LE(vertex.x, 12.0f + 1e-4f);
      EXPECT_GE(vertex.z, -10.0f - 1e-4f);
      EXPECT_LE(vertex.z, -6.0f + 1e-4f);
    }
  }

  EXPECT_TRUE(reachesWestFace);
  EXPECT_TRUE(reachesSouthFace);
}

TEST(FirstPersonTemplate, TheRampsHullIsTheBuiltInRampsHull)
{
  const auto project = projectJson();
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

TEST(FirstPersonTemplate, FourFlatSpawnDiscsWithNoCollider)
{
  LoadedTemplate project;
  ASSERT_TRUE(project.loaded);
  const auto& scene = *project.sceneManager.getCurrentScene();

  const std::vector<std::pair<std::string, glm::vec3>> expected = {
    { "SpawnPoint1", { -4, 0.02f, 12 } }, { "SpawnPoint2", { 4, 0.02f, 12 } },
    { "SpawnPoint3", { -4, 0.02f, 16 } }, { "SpawnPoint4", { 4, 0.02f, 16 } }
  };

  EXPECT_EQ(objectsStartingWith(scene, "SpawnPoint").size(), expected.size());

  for (const auto& [name, position] : expected)
  {
    SCOPED_TRACE(name);
    const auto disc = objectNamed(scene, name);

    EXPECT_EQ(positionOfJson(disc), position);
    EXPECT_EQ(scaleOfJson(disc), glm::vec3(1, 0.02f, 1));

    const auto renderer = componentOf(disc, "ModelRenderer");
    EXPECT_EQ(renderer.at("modelUUID"), cylinderModel);
    EXPECT_EQ(renderer.at("textureUUID"), paletteYellowTexture);
    EXPECT_FALSE(hasComponent(disc, "Collider"));
    EXPECT_FALSE(hasComponent(disc, "RigidBody"));
  }
}

TEST(FirstPersonTemplate, TheSceneHoldsPlayerSpawnManagerAndNoPlacedPlayer)
{
  LoadedTemplate project;
  ASSERT_TRUE(project.loaded);
  const auto& scene = *project.sceneManager.getCurrentScene();

  ASSERT_EQ(scene.getScripts().size(), 1u);
  EXPECT_EQ(scene.getScripts().front()->getClassName(), "PlayerSpawnManager");

  // Positive control: the crates are in the object list this walk covers.
  int objects = 0;
  for (const auto& object : scene.getObjectManager()->getAllObjects())
  {
    ++objects;
    EXPECT_EQ(object->getName().find("Player"), std::string::npos) << object->getName();
    EXPECT_FALSE(hasComponent(object->serialize(), "PlayerController")) << object->getName();
    EXPECT_FALSE(hasComponent(object->serialize(), "Camera")) << object->getName();
    EXPECT_TRUE(object->serialize().at("scripts").empty()) << object->getName();
  }

  EXPECT_EQ(objects, 19);
}

TEST(FirstPersonTemplate, EveryReferencedAssetAndPrefabIsRegistered)
{
  const auto project = projectJson();
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
  LoadedTemplate project;
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
  const auto project = projectJson();
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

TEST(FirstPersonTemplate, CratesStayPutAndNothingFallsOverThreeHundredTicks)
{
  LoadedTemplate project;
  ASSERT_TRUE(project.loaded);

  const auto& objectManager = *project.sceneManager.getCurrentScene()->getObjectManager();

  struct Start
  {
    std::shared_ptr<Object> object;
    glm::vec3 position;
  };

  std::vector<Start> crates;
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

  for (const auto& object : objectManager.getAllObjects())
  {
    const auto position = fixtures::positionOf(object);
    EXPECT_TRUE(std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z))
      << object->getName();
    EXPECT_GT(position.y, -50.0f) << object->getName();
  }

  for (const auto& [object, start] : crates)
  {
    SCOPED_TRACE(object->getName());
    const auto position = fixtures::positionOf(object);

    EXPECT_LT(glm::distance(position, start), 0.05f);

    // The ground holds the bottom crates at their half height.
    if (start.y < 1.0f)
    {
      EXPECT_NEAR(position.y, 0.75f, 0.05f);
    }

    const auto orientation = fixtures::transformOf(object)->getOrientation();
    const float angle = glm::degrees(2.0f * std::acos(std::min(1.0f, std::abs(orientation.w))));
    EXPECT_LT(angle, 2.0f);
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
