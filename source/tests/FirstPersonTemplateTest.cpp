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

  constexpr const char* planeTileModel = "6a742837-aa60-43fe-a652-32a3d385cde0";
  constexpr const char* gridCheckerTexture = "48ad67dd-957f-4fc9-bd7b-019debffa1d0";
  constexpr const char* checkerOrangeTexture = "425f3784-74eb-4e09-bfbb-88facfb014d0";
  constexpr const char* checkerBlueTexture = "05325ffd-75bd-4d76-a50d-7b80e183b081";
  constexpr const char* paletteYellowTexture = "1d5edebf-57f9-499c-9840-b84a57174422";
  constexpr const char* cylinderModel = "a383cc8f-ee6e-4373-ae06-f10f85ef159b";
  constexpr const char* rampModel = "e77194e0-e50e-4070-aacd-2f5d917b2da7";

  bool approx(const float a, const float b)
  {
    return std::abs(a - b) < 1e-4f;
  }


  std::vector<glm::vec3> highEdge(const nlohmann::json& ramp)
  {
    std::vector<glm::vec3> edge;
    for (const auto& vertex : worldHullVertices(ramp))
    {
      if (approx(vertex.y, 2.0f))
      {
        edge.push_back(vertex);
      }
    }

    return edge;
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
  LoadedTemplate project(templateFolder);
  ASSERT_TRUE(project.loaded);

  const auto scene = project.sceneManager.getCurrentScene();
  ASSERT_NE(scene, nullptr);
  EXPECT_EQ(scene->getName(), "Main");
  EXPECT_EQ(project.sceneManager.getScenes().size(), 1u);
}

TEST(FirstPersonTemplate, GroundIsFortyByFortyOfFourGridTiles)
{
  LoadedTemplate project(templateFolder);
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
  LoadedTemplate project(templateFolder);
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
  LoadedTemplate project(templateFolder);
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

TEST(FirstPersonTemplate, ThePlatformIsAStaticBlueBlock)
{
  LoadedTemplate project(templateFolder);
  ASSERT_TRUE(project.loaded);
  const auto platform = objectNamed(*project.sceneManager.getCurrentScene(), "Platform");

  EXPECT_EQ(positionOfJson(platform), glm::vec3(10, 1, -8));
  EXPECT_EQ(scaleOfJson(platform), glm::vec3(2, 1, 2));
  EXPECT_EQ(componentOf(platform, "ModelRenderer").at("textureUUID"), checkerBlueTexture);
  EXPECT_FALSE(hasComponent(platform, "RigidBody"));
  EXPECT_TRUE(hasComponent(platform, "Collider"));
}

TEST(FirstPersonTemplate, TwoStaticRampsRestOnTheGroundAndRiseToThePlatformTop)
{
  LoadedTemplate project(templateFolder);
  ASSERT_TRUE(project.loaded);
  const auto ramps = objectsStartingWith(*project.sceneManager.getCurrentScene(), "Ramp ");
  ASSERT_EQ(ramps.size(), 2u);

  for (const auto& ramp : ramps)
  {
    SCOPED_TRACE(ramp.at("name").get<std::string>());
    EXPECT_EQ(scaleOfJson(ramp), glm::vec3(2, 1, 2));
    EXPECT_FALSE(hasComponent(ramp, "RigidBody"));
    EXPECT_EQ(componentOf(ramp, "ModelRenderer").at("modelUUID"), rampModel);

    const auto vertices = worldHullVertices(ramp);
    ASSERT_EQ(vertices.size(), 6u);
    const auto heights = std::ranges::minmax(vertices, {}, &glm::vec3::y);
    EXPECT_TRUE(approx(heights.min.y, 0.0f));
    EXPECT_TRUE(approx(heights.max.y, 2.0f));
  }
}

TEST(FirstPersonTemplate, EachRampsHighEdgeLiesOnAPlatformFace)
{
  LoadedTemplate project(templateFolder);
  ASSERT_TRUE(project.loaded);
  const auto& scene = *project.sceneManager.getCurrentScene();

  // The platform spans x 8..12, y 0..2, z -10..-6: the west ramp meets x = 8, the south ramp z = -6.
  const auto westEdge = highEdge(objectNamed(scene, "Ramp 1"));
  const auto southEdge = highEdge(objectNamed(scene, "Ramp 2"));
  ASSERT_EQ(westEdge.size(), 2u);
  ASSERT_EQ(southEdge.size(), 2u);

  for (const auto& vertex : westEdge)
  {
    EXPECT_TRUE(approx(vertex.x, 8.0f));
    EXPECT_GE(vertex.z, -10.0f - 1e-4f);
    EXPECT_LE(vertex.z, -6.0f + 1e-4f);
  }

  for (const auto& vertex : southEdge)
  {
    EXPECT_TRUE(approx(vertex.z, -6.0f));
    EXPECT_GE(vertex.x, 8.0f - 1e-4f);
    EXPECT_LE(vertex.x, 12.0f + 1e-4f);
  }
}

TEST(FirstPersonTemplate, FourFlatSpawnDiscsWithNoCollider)
{
  LoadedTemplate project(templateFolder);
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
