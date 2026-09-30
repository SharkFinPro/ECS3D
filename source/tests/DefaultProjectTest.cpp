#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "DefaultProject.h"
#include "PhysicsSystem.h"
#include "ProjectSerializer.h"
#include "TestScene.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
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
  for (const auto& object : sceneObjects(project, "Scene 3"))
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

  // Scene 3 is generated at random, but always as a grid of several hundred bodies.
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

  const auto scene = sceneManager.getCurrentScene();
  ASSERT_NE(scene, nullptr);
  ASSERT_EQ(scene->getName(), "Scene 1");

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

namespace {
  Scene3Options seededScene3(const uint32_t seed, const int grid, const int layers)
  {
    Scene3Options options;
    options.seed = seed;
    options.gridSize = grid;
    options.layerCount = layers;
    options.overlapFree = true;

    return options;
  }

  std::vector<nlohmann::json> bodiesOf(const nlohmann::json& objects)
  {
    std::vector<nlohmann::json> bodies;
    for (const auto& object : objects)
    {
      const auto name = object.at("name").get<std::string>();
      if (name == "Block" || name == "Sphere")
      {
        bodies.push_back(object);
      }
    }

    return bodies;
  }

  glm::vec3 positionOfJson(const nlohmann::json& object)
  {
    for (const auto& component : object.at("components"))
    {
      if (component.at("type") == "Transform")
      {
        return vec3Of(component.at("position"));
      }
    }

    throw std::runtime_error(object.at("name").get<std::string>() + " has no Transform");
  }
}

TEST(DefaultProject, ASeededScene3IsTheSameEveryTimeAndADifferentSeedIsNot)
{
  const auto first = buildDefaultProject(seededScene3(7, 6, 15));
  const auto second = buildDefaultProject(seededScene3(7, 6, 15));
  const auto other = buildDefaultProject(seededScene3(8, 6, 15));

  EXPECT_EQ(sceneObjects(first, "Scene 3"), sceneObjects(second, "Scene 3"));
  EXPECT_NE(sceneObjects(first, "Scene 3"), sceneObjects(other, "Scene 3"));
}

TEST(DefaultProject, ScalingScene3UpStartsEveryBodyClearOfEveryOther)
{
  const auto project = buildDefaultProject(seededScene3(1, 12, 15));
  const auto bodies = bodiesOf(sceneObjects(project, "Scene 3"));

  ASSERT_EQ(bodies.size(), 2160u);

  const float sqrt3 = std::sqrt(3.0f);

  struct Box {
    glm::vec3 center;
    float radius;
  };

  std::vector<Box> boxes;
  for (const auto& body : bodies)
  {
    const auto scale = scaleOf(body);
    boxes.push_back({ positionOfJson(body), std::max({ scale.x, scale.y, scale.z }) * sqrt3 });
  }

  int overlaps = 0;
  for (size_t i = 0; i < boxes.size(); ++i)
  {
    for (size_t j = i + 1; j < boxes.size(); ++j)
    {
      const float reach = boxes[i].radius + boxes[j].radius;
      const auto apart = glm::abs(boxes[i].center - boxes[j].center);

      if (apart.x < reach && apart.y < reach && apart.z < reach)
      {
        ++overlaps;
      }
    }
  }

  EXPECT_EQ(overlaps, 0);
}
