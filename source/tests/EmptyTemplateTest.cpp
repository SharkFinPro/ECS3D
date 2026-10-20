#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "PhysicsSystem.h"
#include "ProjectSerializer.h"
#include "TestScene.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"
#include "templates/TemplateManifest.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace
{
  const std::filesystem::path templateFolder =
    std::filesystem::path(ECS3D_SOURCE_DIR) / "source" / "templates" / "empty-3d";

  constexpr const char* planeTileModel = "6a742837-aa60-43fe-a652-32a3d385cde0";
  constexpr const char* gridCheckerTexture = "48ad67dd-957f-4fc9-bd7b-019debffa1d0";

  glm::vec3 vec3Of(const nlohmann::json& value)
  {
    return { value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>() };
  }

  bool hasComponent(const nlohmann::json& object, const std::string& type)
  {
    for (const auto& component : object.at("components"))
    {
      if (component.at("type") == type)
      {
        return true;
      }
    }

    return false;
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

  nlohmann::json objectNamed(const SceneAsset& scene, const std::string& name)
  {
    for (const auto& object : scene.getObjectManager()->getObjects())
    {
      if (object->getName() == name)
      {
        return object->serialize();
      }
    }

    ADD_FAILURE() << "no object named " << name;
    return nlohmann::json::object();
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

  std::vector<nlohmann::json> transformsOf(const ObjectManager& objectManager)
  {
    std::vector<nlohmann::json> transforms;
    for (const auto& object : objectManager.getAllObjects())
    {
      transforms.push_back(componentOf(object->serialize(), "Transform"));
    }

    return transforms;
  }

  bool allFinite(const nlohmann::json& transform)
  {
    for (const char* key : { "position", "rotation", "scale" })
    {
      for (const auto& value : transform.at(key))
      {
        if (!std::isfinite(value.get<float>()))
        {
          return false;
        }
      }
    }

    return true;
  }
}

namespace
{
  glm::vec3 expectGridTile(const nlohmann::json& tile)
  {
    const auto tileTransform = componentOf(tile, "Transform");
    EXPECT_EQ(vec3Of(tileTransform.at("scale")), glm::vec3(10, 1, 10));

    const auto renderer = componentOf(tile, "ModelRenderer");
    EXPECT_TRUE(renderer.at("shouldRender").get<bool>());
    EXPECT_EQ(renderer.at("modelUUID"), planeTileModel);
    EXPECT_EQ(renderer.at("textureUUID"), gridCheckerTexture);
    EXPECT_FALSE(hasComponent(tile, "Collider"));

    return vec3Of(tileTransform.at("position"));
  }

  int countMatches(const std::vector<glm::vec3>& positions, const glm::vec3& expected)
  {
    int matches = 0;
    for (const auto& position : positions)
    {
      matches += position == expected ? 1 : 0;
    }

    return matches;
  }

  std::vector<std::string> registeredAssetUuids(const nlohmann::json& project)
  {
    std::vector<std::string> registered;
    for (const char* list : { "models", "textures" })
    {
      for (const auto& asset : project.at("assets").at(list))
      {
        registered.push_back(asset.at("uuid").get<std::string>());
        EXPECT_EQ(asset.at("filePath").get<std::string>().rfind("assets/", 0), 0u);
      }
    }

    return registered;
  }

  bool contains(const std::vector<std::string>& values, const std::string& wanted)
  {
    return std::find(values.begin(), values.end(), wanted) != values.end();
  }

  // Returns how many asset references the object and its children hold, expecting each to be registered.
  int checkReferences(const nlohmann::json& object, const std::vector<std::string>& registered)
  {
    int references = 0;
    for (const auto& component : object.at("components"))
    {
      if (component.at("type") != "ModelRenderer")
      {
        continue;
      }

      for (const char* key : { "modelUUID", "textureUUID", "specularMapUUID" })
      {
        EXPECT_TRUE(contains(registered, component.at(key).get<std::string>())) << key;
        ++references;
      }
    }

    for (const auto& child : object.at("children"))
    {
      references += checkReferences(child, registered);
    }

    return references;
  }

  std::shared_ptr<Object> dropBlock(ObjectManager& objectManager)
  {
    const auto block = std::make_shared<Object>("Block");
    objectManager.addObject(block);
    fixtures::transformOf(block)->setPosition(glm::vec3(0, 3, 0));
    fixtures::addBoxCollider(block)->setScale(glm::vec3(1));
    fixtures::addRigidBody(block)->setMass(8.0f);

    return block;
  }

  void tick(const ObjectManager& objectManager, const int count)
  {
    CollisionSystem collisionSystem;
    for (int i = 0; i < count; ++i)
    {
      PhysicsSystem::fixedUpdate(objectManager, 1.0f / 50.0f);
      collisionSystem.fixedUpdate(objectManager, 1.0f / 50.0f);
    }
  }

  void expectRestingOnGround(const std::shared_ptr<Object>& block)
  {
    const auto position = fixtures::positionOf(block);
    EXPECT_TRUE(std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z));
    EXPECT_NEAR(position.y, 1.0f, 0.05f);

    const auto body = block->getComponent<RigidBody>(ComponentType::rigidBody);
    ASSERT_NE(body, nullptr);
    EXPECT_LT(glm::length(body->getVelocity()), 0.05f);
    EXPECT_LT(glm::length(body->getAngularVelocity()), 1.0f);
  }
}

TEST(EmptyTemplate, ManifestParsesWithTheShippedFields)
{
  std::string error;
  const auto manifest = loadTemplateManifest(templateFolder, &error);
  ASSERT_TRUE(manifest.has_value()) << error;

  EXPECT_EQ(manifest->id, "empty-3d");
  EXPECT_EQ(manifest->name, "Empty 3D");
  EXPECT_EQ(manifest->category, "template");
  EXPECT_EQ(manifest->tags, std::vector<std::string>{ "starter" });
  EXPECT_EQ(manifest->thumbnail, "thumbnail.png");
  EXPECT_EQ(manifest->entryScene, "Main");
  EXPECT_EQ(manifest->minEngineVersion, "0.0.1");
  EXPECT_FALSE(manifest->players.has_value());
  EXPECT_FALSE(manifest->description.empty());
}

TEST(EmptyTemplate, ProjectLoadsWithMainAsTheCurrentScene)
{
  LoadedTemplate project;
  ASSERT_TRUE(project.loaded);

  const auto scene = project.sceneManager.getCurrentScene();
  ASSERT_NE(scene, nullptr);
  EXPECT_EQ(scene->getName(), "Main");
  EXPECT_EQ(project.sceneManager.getScenes().size(), 1u);
}

TEST(EmptyTemplate, GroundIsFortyByFortyOfFourGridTiles)
{
  LoadedTemplate project;
  ASSERT_TRUE(project.loaded);
  const auto ground = objectNamed(*project.sceneManager.getCurrentScene(), "Ground");

  const auto transform = componentOf(ground, "Transform");
  EXPECT_EQ(vec3Of(transform.at("position")), glm::vec3(0, -1, 0));
  EXPECT_EQ(vec3Of(transform.at("scale")), glm::vec3(1));

  EXPECT_EQ(vec3Of(componentOf(ground, "Collider", "Box").at("scale")), glm::vec3(20, 1, 20));
  EXPECT_FALSE(componentOf(ground, "ModelRenderer").at("shouldRender").get<bool>());
  EXPECT_FALSE(hasComponent(ground, "RigidBody"));

  ASSERT_EQ(ground.at("children").size(), 4u);
  std::vector<glm::vec3> positions;
  for (const auto& tile : ground.at("children"))
  {
    positions.push_back(expectGridTile(tile));
  }

  for (const auto& expected : { glm::vec3(-10, 1, -10), glm::vec3(10, 1, -10),
                                glm::vec3(-10, 1, 10), glm::vec3(10, 1, 10) })
  {
    EXPECT_EQ(countMatches(positions, expected), 1) << expected.x << ", " << expected.z;
  }
}

TEST(EmptyTemplate, HasOneSunAndAnActiveMainCamera)
{
  LoadedTemplate project;
  ASSERT_TRUE(project.loaded);
  const auto& scene = *project.sceneManager.getCurrentScene();

  int lights = 0;
  for (const auto& object : scene.getObjectManager()->getAllObjects())
  {
    lights += object->getComponent(ComponentType::lightRenderer) ? 1 : 0;
  }

  EXPECT_EQ(lights, 1);

  const auto sun = objectNamed(scene, "Sun");
  EXPECT_EQ(vec3Of(componentOf(sun, "Transform").at("position")), glm::vec3(10, 25, 10));

  const auto light = componentOf(sun, "LightRenderer");
  EXPECT_FALSE(light.at("isSpotlight").get<bool>());
  EXPECT_NEAR(glm::distance(vec3Of(light.at("color")), glm::vec3(1.0f, 0.97f, 0.9f)), 0.0f, 1e-5f);
  EXPECT_NEAR(light.at("ambient").get<float>(), 0.25f, 1e-6f);
  EXPECT_NEAR(light.at("diffuse").get<float>(), 0.8f, 1e-6f);
  EXPECT_NEAR(light.at("specular").get<float>(), 0.5f, 1e-6f);

  const auto camera = objectNamed(scene, "Main Camera");
  EXPECT_EQ(vec3Of(componentOf(camera, "Transform").at("position")), glm::vec3(0, 6, 18));

  const auto cameraComponent = componentOf(camera, "Camera");
  EXPECT_TRUE(cameraComponent.at("active").get<bool>());
  const auto expected = glm::normalize(glm::vec3(0, -6, -18));
  EXPECT_NEAR(glm::distance(vec3Of(cameraComponent.at("direction")), expected), 0.0f, 1e-4f);
}

TEST(EmptyTemplate, EveryReferencedAssetIsRegistered)
{
  std::ifstream file(templateFolder / "project.ecs3d");
  ASSERT_TRUE(file.is_open());
  const auto project = nlohmann::json::parse(file);
  const auto registered = registeredAssetUuids(project);

  // Ground and four tiles, three references each; the positive control for the walk.
  int references = 0;
  for (const auto& object : project.at("assets").at("scenes").at(0).at("objects"))
  {
    references += checkReferences(object, registered);
  }

  EXPECT_EQ(references, 15);
}

TEST(EmptyTemplate, AGroundCatchesADroppedBlockAndNothingElseMoves)
{
  LoadedTemplate project;
  ASSERT_TRUE(project.loaded);

  const auto objectManagerPtr = project.sceneManager.getCurrentScene()->getObjectManager();
  auto& objectManager = *objectManagerPtr;
  const auto before = transformsOf(objectManager);
  ASSERT_EQ(before.size(), 7u);

  // The template holds nothing dynamic, so a Block like DefaultProject's is the positive control.
  const auto block = dropBlock(objectManager);
  tick(objectManager, 300);

  const auto after = transformsOf(objectManager);
  ASSERT_EQ(after.size(), before.size() + 1);
  for (std::size_t i = 0; i < before.size(); ++i)
  {
    EXPECT_TRUE(allFinite(after[i]));
    EXPECT_EQ(after[i], before[i]);
  }

  expectRestingOnGround(block);
}

TEST(EmptyTemplate, ThumbnailIs640By360)
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
