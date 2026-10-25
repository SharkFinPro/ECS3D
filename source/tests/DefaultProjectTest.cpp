#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "DefaultProject.h"
#include "DefaultShapes.h"
#include "InertiaTensor.h"
#include "PhysicsSystem.h"
#include "ProjectSerializer.h"
#include "TestScene.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/ConvexHullValidation.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {
  // The server's tick.
  constexpr float dt = 1.0f / 50.0f;

  constexpr float unitBlockMass = 8.0f;
  constexpr float unitSphereMass = 4.0f / 3.0f * 3.14159265f;

  const nlohmann::json& rigidBodyIn(const nlohmann::json& object)
  {
    for (const auto& component : object.at("components"))
    {
      if (component.at("type") == "RigidBody")
      {
        return component;
      }
    }

    throw std::runtime_error(object.at("name").get<std::string>() + " has no RigidBody");
  }

  const nlohmann::json& prefabBody(const nlohmann::json& project, const std::string& name)
  {
    for (const auto& prefab : project.at("assets").at("prefabs"))
    {
      if (prefab.at("name") == name)
      {
        return prefab.at("body");
      }
    }

    throw std::runtime_error("no prefab named " + name);
  }

  const nlohmann::json& sceneObjects(const nlohmann::json& project, const std::string& name)
  {
    for (const auto& scene : project.at("assets").at("scenes"))
    {
      if (scene.at("name") == name)
      {
        return scene.at("objects");
      }
    }

    throw std::runtime_error("no scene named " + name);
  }

  glm::vec3 vec3Of(const nlohmann::json& value)
  {
    return { value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>() };
  }

  glm::vec3 scaleOf(const nlohmann::json& object)
  {
    for (const auto& component : object.at("components"))
    {
      if (component.at("type") == "Transform")
      {
        return vec3Of(component.at("scale"));
      }
    }

    throw std::runtime_error(object.at("name").get<std::string>() + " has no Transform");
  }

  const nlohmann::json* assetWithUUID(const nlohmann::json& project, const std::string& uuid)
  {
    for (const char* list : { "models", "textures", "prefabs" })
    {
      for (const auto& asset : project.at("assets").at(list))
      {
        if (asset.at("uuid") == uuid)
        {
          return &asset;
        }
      }
    }

    return nullptr;
  }

  // Components keyed by type (plus subType for colliders), since their array order is unspecified.
  std::map<std::string, nlohmann::json> componentsByType(const nlohmann::json& object)
  {
    std::map<std::string, nlohmann::json> byType;
    for (const auto& component : object.at("components"))
    {
      std::string key = component.at("type").get<std::string>();
      if (component.contains("subType"))
      {
        key += "/" + component.at("subType").get<std::string>();
      }

      byType.emplace(std::move(key), component);
    }

    return byType;
  }

  const std::vector<std::pair<std::string, std::string>> newModels = {
    { "953f4317-65aa-4eb0-b186-c0b94bec185e", "assets/models/capsule.glb" },
    { "a383cc8f-ee6e-4373-ae06-f10f85ef159b", "assets/models/cylinder.glb" },
    { "e77194e0-e50e-4070-aacd-2f5d917b2da7", "assets/models/ramp.glb" },
    { "6a742837-aa60-43fe-a652-32a3d385cde0", "assets/models/plane_tile.glb" },
    { "7a1cd753-3775-44a0-858f-27239f8740f5", "assets/models/torus.glb" },
    { "4e4e2499-ff78-43d8-998e-c7cfc149337e", "assets/models/d4.glb" },
    { "6539110c-cfb2-4d1e-8888-6dcefc76a66b", "assets/models/d8.glb" },
    { "5f4873d6-d2c7-4313-b232-728b949f1dd6", "assets/models/d12.glb" },
    { "f810cee9-5c0b-47f6-9943-ae185c0105bd", "assets/models/d20.glb" },
    { "dc62625e-9c71-4de3-9a1d-56e6d7ba47c0", "assets/models/hex_prism.glb" },
    { "75fac0ef-08ca-4c20-81fa-de5aa9269d50", "assets/models/drum.glb" },
    { "ad754062-1662-4446-8aac-aefa926d49b6", "assets/models/gem.glb" },
    { "9d0e47fd-8a6f-4ff9-9614-ddb0c5dddd5a", "assets/models/boulder.glb" }
  };

  const std::vector<std::pair<std::string, std::string>> newTextures = {
    { "48ad67dd-957f-4fc9-bd7b-019debffa1d0", "assets/textures/grid_checker.png" },
    { "425f3784-74eb-4e09-bfbb-88facfb014d0", "assets/textures/checker_orange.png" },
    { "05325ffd-75bd-4d76-a50d-7b80e183b081", "assets/textures/checker_blue.png" },
    { "7c8c01db-771f-4048-94c2-004b8d19e7aa", "assets/textures/palette_red.png" },
    { "c5eee5e6-9bd0-4f9b-977f-b9220a21a630", "assets/textures/palette_green.png" },
    { "66a8a6fc-d151-4567-9ce5-2e17cc675fe0", "assets/textures/palette_blue.png" },
    { "1d5edebf-57f9-499c-9840-b84a57174422", "assets/textures/palette_yellow.png" },
    { "ea184ad8-ca64-48cd-b69e-92bcb4649158", "assets/textures/palette_orange.png" },
    { "8925b024-c923-4839-9e6b-fa67e69e6752", "assets/textures/palette_purple.png" }
  };

  ConvexHull hullOfCollider(const nlohmann::json& collider)
  {
    ConvexHull hull;
    const auto& flat = collider.at("vertices");
    for (size_t i = 0; i < flat.size(); i += 3)
    {
      hull.vertices.emplace_back(flat[i].get<float>(), flat[i + 1].get<float>(), flat[i + 2].get<float>());
    }
    hull.indices = collider.at("indices").get<std::vector<uint32_t>>();

    return hull;
  }

  bool isFinite(const glm::vec3& value)
  {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
  }
}

TEST(DefaultProject, ThePrefabsWeighWhatTheirVolumeDoes)
{
  const auto project = buildDefaultProject();

  // A density of one: the unit Block is two units a side, the Sphere and the Player have a radius of one.
  EXPECT_NEAR(rigidBodyIn(prefabBody(project, "Block")).at("mass").get<float>(), unitBlockMass, 1e-3f);
  EXPECT_NEAR(rigidBodyIn(prefabBody(project, "Sphere")).at("mass").get<float>(), unitSphereMass, 1e-3f);
  EXPECT_NEAR(rigidBodyIn(prefabBody(project, "Player")).at("mass").get<float>(), unitSphereMass, 1e-3f);

  // PlayerScript steers the player itself, so its contacts have no friction to fight it; props do.
  EXPECT_FLOAT_EQ(rigidBodyIn(prefabBody(project, "Player")).at("friction").get<float>(), 0.0f);
  EXPECT_FLOAT_EQ(rigidBodyIn(prefabBody(project, "Block")).at("friction").get<float>(), 0.5f);
}

TEST(DefaultProject, AScaledInstanceKeepsItsPrefabsDensity)
{
  const auto project = buildDefaultProject();

  int checked = 0;
  for (const auto& object : sceneObjects(project, "Tower Collapse"))
  {
    const auto name = object.at("name").get<std::string>();
    if (name != "Block" && name != "Sphere")
    {
      continue;
    }

    const auto scale = scaleOf(object);
    const float radius = std::max({ scale.x, scale.y, scale.z });
    const float expected = name == "Block" ? unitBlockMass * scale.x * scale.y * scale.z
                                           : unitSphereMass * radius * radius * radius;

    SCOPED_TRACE(name);
    EXPECT_NEAR(rigidBodyIn(object).at("mass").get<float>(), expected, expected * 1e-4f);
    ++checked;
  }

  // Tower Collapse is generated at random, but always as a grid of several hundred bodies.
  EXPECT_GT(checked, 100);
}

TEST(DefaultProject, LoadsAndItsFirstSceneSettles)
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
    if (candidate->getName() == "Lights and Players")
    {
      scene = candidate;
    }
  }

  ASSERT_NE(scene, nullptr);
  sceneManager.loadScene(scene);
  ASSERT_EQ(sceneManager.getCurrentScene(), scene);

  const auto& objectManager = *scene->getObjectManager();
  CollisionSystem collisionSystem;

  // Ten seconds of the server's ticks.
  for (int tick = 0; tick < 500; ++tick)
  {
    PhysicsSystem::fixedUpdate(objectManager, dt);
    collisionSystem.fixedUpdate(objectManager, dt);
  }

  std::shared_ptr<Object> block;
  for (const auto& object : objectManager.getAllObjects())
  {
    const auto body = object->getComponent<RigidBody>(ComponentType::rigidBody);
    if (!body)
    {
      continue;
    }

    SCOPED_TRACE(object->getName());
    EXPECT_TRUE(isFinite(fixtures::positionOf(object)));
    EXPECT_TRUE(isFinite(body->getVelocity()));
    EXPECT_TRUE(isFinite(body->getAngularVelocity()));

    if (object->getName() == "Block")
    {
      block = object;
    }
  }

  // The Block drops straight onto the middle of the ground, whose top is at -9, clear of everything else.
  ASSERT_NE(block, nullptr);
  const auto blockBody = block->getComponent<RigidBody>(ComponentType::rigidBody);
  EXPECT_NEAR(fixtures::positionOf(block).y, -8.0f, 0.1f);
  EXPECT_LT(glm::length(blockBody->getVelocity()), 0.01f);
  EXPECT_LT(glm::length(blockBody->getAngularVelocity()), 0.05f);
}

TEST(DefaultProject, TheNewModelsAndTexturesKeepTheirUuids)
{
  const auto project = buildDefaultProject();

  for (const auto& [uuid, path] : newModels)
  {
    SCOPED_TRACE(path);
    const auto* asset = assetWithUUID(project, uuid);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->at("filePath"), path);
    EXPECT_EQ(asset->at("name"), path);
  }

  for (const auto& [uuid, path] : newTextures)
  {
    SCOPED_TRACE(path);
    const auto* asset = assetWithUUID(project, uuid);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->at("filePath"), path);
  }

  // Positive control for the lookup: an absent uuid is not found.
  EXPECT_EQ(assetWithUUID(project, "00000000-0000-4000-8000-000000000000"), nullptr);
}

TEST(DefaultProject, EveryAssetFileExistsInTheDefaultAssetsFolder)
{
  const auto project = buildDefaultProject();
  const auto assets = std::filesystem::path(ECS3D_SOURCE_DIR) / "source" / "apps" / "server" / "defaultAssets";

  size_t checked = 0;
  for (const char* list : { "models", "textures" })
  {
    for (const auto& asset : project.at("assets").at(list))
    {
      const auto filePath = asset.at("filePath").get<std::string>();
      ASSERT_TRUE(filePath.starts_with("assets/")) << filePath;

      EXPECT_TRUE(std::filesystem::is_regular_file(assets / filePath.substr(7))) << filePath;
      ++checked;
    }
  }

  // The checks above would pass vacuously on an empty list.
  EXPECT_GE(checked, newModels.size() + newTextures.size());
}

TEST(DefaultProject, TheNewPrefabsAreRegisteredWithStableUuids)
{
  const auto project = buildDefaultProject();

  const std::vector<std::pair<std::string, std::string>> prefabs = {
    { "Capsule", "39e415e0-ff7a-475c-b677-72062ce2954a" },
    { "Cylinder", "62f97ec2-a58e-4e6d-b9e4-a466dc04669a" },
    { "Ramp", "1b112469-4331-402e-bc4a-94cd0368ef52" }
  };

  for (const auto& [name, uuid] : prefabs)
  {
    SCOPED_TRACE(name);
    const auto* asset = assetWithUUID(project, uuid);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->at("name"), name);
    EXPECT_EQ(asset->at("body").at("name"), name);
  }
}

TEST(DefaultProject, TheNewPrefabsRoundTripAndInstantiate)
{
  const auto project = buildDefaultProject();

  for (const char* name : { "Capsule", "Cylinder", "Ramp", "D4", "D6", "D8", "D12", "D20", "Hex Prism", "Drum",
                            "Gem", "Boulder" })
  {
    SCOPED_TRACE(name);
    const auto& body = prefabBody(project, name);

    // Restoring keeps the uuids. One pass through an object normalizes whatever the builders omit, and a
    // second pass must then change nothing.
    const auto first = fixtures::makeScene();
    const auto once = first.objectManager->restoreSubtree(body, nullptr, 0)->serialize();

    const auto second = fixtures::makeScene();
    const auto twice = second.objectManager->restoreSubtree(once, nullptr, 0)->serialize();

    EXPECT_EQ(once.at("uuid"), twice.at("uuid"));
    EXPECT_EQ(componentsByType(once), componentsByType(twice));
    EXPECT_EQ(componentsByType(once).size(), body.at("components").size());

    // Every collider field the prefab authors survives the trip, as do the model and texture it names.
    const auto authored = componentsByType(body);
    const auto loaded = componentsByType(once);
    for (const char* key : { "Collider/Capsule", "Collider/ConvexMesh" })
    {
      if (authored.contains(key))
      {
        EXPECT_EQ(authored.at(key), loaded.at(key)) << key;
      }
    }

    for (const char* field : { "modelUUID", "textureUUID" })
    {
      EXPECT_FALSE(authored.at("ModelRenderer").at(field).get<std::string>().empty());
      EXPECT_EQ(authored.at("ModelRenderer").at(field), loaded.at("ModelRenderer").at(field)) << field;
    }

    const auto scene = fixtures::makeScene();
    const auto instance = scene.objectManager->instantiate(body);
    ASSERT_NE(instance, nullptr);
    EXPECT_EQ(instance->getName(), name);
    EXPECT_NE(instance->serialize().at("uuid"), body.at("uuid"));
  }
}

TEST(DefaultProject, TheCapsuleAndCylinderWeighTheirVolume)
{
  const auto project = buildDefaultProject();
  // The mass is the volume of the collider the prefab really carries.
  const auto& capsule = prefabBody(project, "Capsule");
  const auto scene = fixtures::makeScene();
  const auto instance = scene.objectManager->instantiate(capsule);
  const auto shapes = inertia::bodyDistributions(*instance);
  ASSERT_EQ(shapes.size(), 1u);
  EXPECT_GT(shapes.front().volume, 10.0f);
  EXPECT_NEAR(rigidBodyIn(capsule).at("mass").get<float>(), shapes.front().volume, 1e-2f);
  EXPECT_FLOAT_EQ(rigidBodyIn(capsule).at("friction").get<float>(), 0.5f);

  const auto collider = componentsByType(capsule).at("Collider/Capsule");
  EXPECT_FLOAT_EQ(collider.at("radius").get<float>(), 1.0f);
  EXPECT_FLOAT_EQ(collider.at("height").get<float>(), 4.0f);

  const auto& cylinder = prefabBody(project, "Cylinder");
  const float volume = hullVolume(hullOfCollider(componentsByType(cylinder).at("Collider/ConvexMesh")));
  EXPECT_NEAR(rigidBodyIn(cylinder).at("mass").get<float>(), volume, 1e-3f);
  EXPECT_GT(volume, 6.0f);
  EXPECT_FLOAT_EQ(rigidBodyIn(cylinder).at("friction").get<float>(), 0.5f);
}

TEST(DefaultProject, TheRampIsStaticAndTheOthersAreBodies)
{
  const auto project = buildDefaultProject();

  EXPECT_FALSE(componentsByType(prefabBody(project, "Ramp")).contains("RigidBody"));
  EXPECT_TRUE(componentsByType(prefabBody(project, "Cylinder")).contains("RigidBody"));
  EXPECT_TRUE(componentsByType(prefabBody(project, "Capsule")).contains("RigidBody"));
}

TEST(DefaultProject, TheHullPrefabsHoldValidHullsNamingTheirModels)
{
  const auto project = buildDefaultProject();

  const std::vector<std::tuple<std::string, std::string, size_t>> hulls = {
    { "Ramp", "e77194e0-e50e-4070-aacd-2f5d917b2da7", 6 },
    { "Cylinder", "a383cc8f-ee6e-4373-ae06-f10f85ef159b", 64 }
  };

  for (const auto& [name, model, vertexCount] : hulls)
  {
    SCOPED_TRACE(name);
    const auto collider = componentsByType(prefabBody(project, name)).at("Collider/ConvexMesh");
    const auto hull = hullOfCollider(collider);

    EXPECT_EQ(hull.vertices.size(), vertexCount);
    EXPECT_TRUE(isValidConvexHull(hull));
    EXPECT_GT(hullVolume(hull), 0.0f);
    EXPECT_EQ(collider.at("sourceModel"), model);
  }
}

TEST(DefaultProject, TheShapeLibraryPrefabsAreRegisteredWithStableUuidsAndWeighTheirVolume)
{
  struct Entry
  {
    const char* name;
    const char* uuid;
    const char* model;
    ConvexHull (*hull)();
  };

  const Entry entries[] = {
    { "D4", "d973cf42-3ef2-4ea6-a4a4-e505fa2d0c17", "4e4e2499-ff78-43d8-998e-c7cfc149337e", d4Hull },
    { "D6", "f02de30b-fa89-400f-b2ba-fbabcd219775", "58034695-0495-4cfb-8f75-90cf24a9073b", d6Hull },
    { "D8", "fab33a9c-0c55-488f-b5ab-516cc7558075", "6539110c-cfb2-4d1e-8888-6dcefc76a66b", d8Hull },
    { "D12", "c901eb04-d6b1-480b-a556-19f9fba037c7", "5f4873d6-d2c7-4313-b232-728b949f1dd6", d12Hull },
    { "D20", "32cf047d-3fa4-40a5-9dbd-5377aa5bbaa1", "f810cee9-5c0b-47f6-9943-ae185c0105bd", d20Hull },
    { "Hex Prism", "cb56959b-90d6-4b14-b2bb-64bc5e3b08f1", "dc62625e-9c71-4de3-9a1d-56e6d7ba47c0", hexPrismHull },
    { "Drum", "41980fde-c3d9-4672-87a3-71d80acdc734", "75fac0ef-08ca-4c20-81fa-de5aa9269d50", drumHull },
    { "Gem", "98f4b839-f256-4562-b8ff-266094c98725", "ad754062-1662-4446-8aac-aefa926d49b6", gemHull },
    { "Boulder", "dca2e8d6-7dab-4706-84f2-61da7ad076f1", "9d0e47fd-8a6f-4ff9-9614-ddb0c5dddd5a", boulderHull }
  };

  const auto project = buildDefaultProject();

  for (const auto& entry : entries)
  {
    SCOPED_TRACE(entry.name);
    const auto* asset = assetWithUUID(project, entry.uuid);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->at("name"), entry.name);

    const auto& body = asset->at("body");
    const auto collider = componentsByType(body).at("Collider/ConvexMesh");
    EXPECT_EQ(collider.at("sourceModel"), entry.model);
    EXPECT_EQ(componentsByType(body).at("ModelRenderer").at("modelUUID"), entry.model);
    EXPECT_EQ(componentsByType(body).at("ModelRenderer").at("textureUUID"), "425f3784-74eb-4e09-bfbb-88facfb014d0");
    EXPECT_NE(assetWithUUID(project, entry.model), nullptr);

    const float volume = hullVolume(entry.hull());
    EXPECT_GT(volume, 0.1f);
    EXPECT_NEAR(rigidBodyIn(body).at("mass").get<float>(), volume, volume * 1e-4f);
    EXPECT_FLOAT_EQ(rigidBodyIn(body).at("friction").get<float>(), 0.5f);
    EXPECT_EQ(hullOfCollider(collider).vertices.size(), entry.hull().vertices.size());
  }

  // The unit cube hull weighs what the Block does.
  EXPECT_NEAR(rigidBodyIn(prefabBody(project, "D6")).at("mass").get<float>(), unitBlockMass, 1e-3f);
}

TEST(DefaultProject, TheMissingAssetPlaceholdersShipButAreNeverRegistered)
{
  const auto project = buildDefaultProject();
  const auto assets = std::filesystem::path(ECS3D_SOURCE_DIR) / "source" / "apps" / "server" / "defaultAssets";

  EXPECT_TRUE(std::filesystem::is_regular_file(assets / "models" / "missing_model.glb"));
  EXPECT_TRUE(std::filesystem::is_regular_file(assets / "textures" / "missing_texture.png"));

  bool foundRegistered = false;
  for (const char* list : { "models", "textures" })
  {
    for (const auto& asset : project.at("assets").at(list))
    {
      const auto filePath = asset.at("filePath").get<std::string>();
      EXPECT_EQ(filePath.find("missing_"), std::string::npos) << filePath;
      foundRegistered = foundRegistered || filePath == "assets/textures/grid_checker.png";
    }
  }

  // Positive control: the scan reaches a registered texture.
  EXPECT_TRUE(foundRegistered);
}
