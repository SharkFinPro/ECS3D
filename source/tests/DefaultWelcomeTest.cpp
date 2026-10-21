#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "DefaultProject.h"
#include "PhysicsSystem.h"
#include "ProjectSerializer.h"
#include "TestScene.h"
#include "assets/AssetRegistry.h"
#include "collisions/NarrowPhase.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/Collider.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {
  constexpr float dt = 1.0f / 50.0f;

  using ClassFields = std::map<std::string, std::set<std::string>>;

  // Every class declared under UserScripts and the [ExposeToEditor] fields declared after it in its file.
  ClassFields shippedScriptClasses()
  {
    const auto root = std::filesystem::path(ECS3D_SOURCE_DIR) / "source" / "libs" / "scripting" / "UserScripts";
    const std::regex classPattern(R"(\bclass\s+(\w+))");
    const std::regex fieldPattern(R"(\[ExposeToEditor[^\]]*\]\s*(?:public|private|protected|internal)?\s*[\w<>]+\s+(\w+))");

    ClassFields classes;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
    {
      if (entry.path().extension() != ".cs")
      {
        continue;
      }

      std::ifstream stream(entry.path());
      const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

      std::vector<std::pair<size_t, std::string>> declared;
      for (std::sregex_iterator it(text.begin(), text.end(), classPattern), end; it != end; ++it)
      {
        declared.emplace_back(static_cast<size_t>(it->position()), (*it)[1].str());
        classes[(*it)[1].str()];
      }

      for (std::sregex_iterator it(text.begin(), text.end(), fieldPattern), end; it != end; ++it)
      {
        const auto position = static_cast<size_t>(it->position());
        const std::string* owner = nullptr;
        for (const auto& [at, name] : declared)
        {
          if (at < position)
          {
            owner = &name;
          }
        }

        if (owner)
        {
          classes[*owner].insert((*it)[1].str());
        }
      }
    }

    return classes;
  }

  void collectScripts(const nlohmann::json& object, std::vector<nlohmann::json>& scripts)
  {
    if (object.contains("scripts"))
    {
      for (const auto& script : object.at("scripts"))
      {
        scripts.push_back(script);
      }
    }

    if (object.contains("children"))
    {
      for (const auto& child : object.at("children"))
      {
        collectScripts(child, scripts);
      }
    }
  }

  void collectAssetReferences(const nlohmann::json& object, std::vector<std::string>& uuids)
  {
    for (const auto& component : object.at("components"))
    {
      for (const char* key : { "modelUUID", "textureUUID", "specularMapUUID", "sourceModel" })
      {
        if (component.contains(key) && !component.at(key).get<std::string>().empty())
        {
          uuids.push_back(component.at(key).get<std::string>());
        }
      }
    }

    for (const auto& child : object.at("children"))
    {
      collectAssetReferences(child, uuids);
    }
  }

  std::set<std::string> assetUuids(const nlohmann::json& project)
  {
    std::set<std::string> uuids;
    for (const char* list : { "models", "textures", "prefabs", "scripts" })
    {
      for (const auto& asset : project.at("assets").at(list))
      {
        uuids.insert(asset.at("uuid").get<std::string>());
      }
    }

    return uuids;
  }

  const nlohmann::json& sceneNamed(const nlohmann::json& project, const std::string& name)
  {
    for (const auto& scene : project.at("assets").at("scenes"))
    {
      if (scene.at("name") == name)
      {
        return scene;
      }
    }

    throw std::runtime_error("no scene named " + name);
  }

  struct LoadedProject
  {
    std::shared_ptr<ComponentRegistry> componentRegistry = std::make_shared<ComponentRegistry>();
    AssetRegistry assetRegistry;
    SceneManager sceneManager;
  };

  void load(LoadedProject& loaded, const nlohmann::json& project)
  {
    registerDataComponents(*loaded.componentRegistry);
    const ProjectSerializer serializer(&loaded.assetRegistry, &loaded.sceneManager, loaded.componentRegistry);
    serializer.deserialize(project);
  }

  bool isFinite(const glm::vec3& value)
  {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
  }

  std::vector<std::shared_ptr<Object>> towerBlocks(const ObjectManager& objectManager)
  {
    std::vector<std::shared_ptr<Object>> blocks;
    for (const auto& object : objectManager.getAllObjects())
    {
      if (object->getName() == "Tower Block")
      {
        blocks.push_back(object);
      }
    }

    return blocks;
  }
}

TEST(DefaultWelcome, IsTheFirstAndCurrentSceneAndTheOthersAreRenamed)
{
  const auto project = buildDefaultProject();
  const auto& scenes = project.at("assets").at("scenes");

  ASSERT_EQ(scenes.size(), 5u);
  EXPECT_EQ(scenes.at(0).at("name"), "Welcome");
  EXPECT_EQ(project.at("currentSceneUUID"), scenes.at(0).at("uuid"));

  std::vector<std::string> names;
  for (const auto& scene : scenes)
  {
    names.push_back(scene.at("name").get<std::string>());
  }

  for (const char* name : { "Lights and Players", "Ramps", "Tower Collapse", "Falling Balls" })
  {
    EXPECT_NE(std::find(names.begin(), names.end(), name), names.end()) << name;
  }

  for (const char* gone : { "Scene 1", "Scene 2", "Scene 3" })
  {
    EXPECT_EQ(std::find(names.begin(), names.end(), gone), names.end()) << gone;
  }

  LoadedProject loaded;
  load(loaded, project);
  ASSERT_NE(loaded.sceneManager.getCurrentScene(), nullptr);
  EXPECT_EQ(loaded.sceneManager.getCurrentScene()->getName(), "Welcome");
}

TEST(DefaultWelcome, EveryScriptClassAndFieldItNamesIsShipped)
{
  const auto classes = shippedScriptClasses();
  ASSERT_TRUE(classes.contains("Spinner"));
  ASSERT_TRUE(classes.at("Spinner").contains("m_degreesPerSecond"));
  ASSERT_FALSE(classes.at("Spinner").contains("m_noSuchField"));

  const auto project = buildDefaultProject();
  const auto known = assetUuids(project);
  const std::regex uuidPattern(R"([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})");

  std::vector<nlohmann::json> scripts;
  const auto& welcome = sceneNamed(project, "Welcome");
  for (const auto& object : welcome.at("objects"))
  {
    collectScripts(object, scripts);
  }

  for (const auto& script : welcome.at("scripts"))
  {
    scripts.push_back(script);
  }

  for (const auto& prefab : project.at("assets").at("prefabs"))
  {
    collectScripts(prefab.at("body"), scripts);
  }

  // Positive control: the Welcome scene really uses scripts.
  EXPECT_GT(scripts.size(), 15u);

  for (const auto& script : scripts)
  {
    const auto className = script.at("className").get<std::string>();
    SCOPED_TRACE(className);
    ASSERT_TRUE(classes.contains(className));

    for (const auto& field : script.at("fields"))
    {
      const auto name = field.at("name").get<std::string>();
      SCOPED_TRACE(name);
      EXPECT_TRUE(classes.at(className).contains(name));

      if (field.at("value").is_string())
      {
        const auto value = field.at("value").get<std::string>();
        if (std::regex_match(value, uuidPattern))
        {
          EXPECT_TRUE(known.contains(value)) << "not an asset or prefab uuid: " << value;
        }
      }
    }
  }
}

TEST(DefaultWelcome, EveryAssetItReferencesIsRegistered)
{
  const auto project = buildDefaultProject();
  const auto known = assetUuids(project);

  std::vector<std::string> references;
  const auto& welcome = sceneNamed(project, "Welcome");
  for (const auto& object : welcome.at("objects"))
  {
    collectAssetReferences(object, references);
  }

  for (const auto& script : welcome.at("scripts"))
  {
    for (const auto& field : script.at("fields"))
    {
      if (field.at("name") == "m_playerPrefab")
      {
        references.push_back(field.at("value").get<std::string>());
      }
    }
  }

  EXPECT_GT(references.size(), 100u);
  for (const auto& reference : references)
  {
    EXPECT_TRUE(known.contains(reference)) << reference;
  }

  // Positive control for the lookup.
  EXPECT_FALSE(known.contains("00000000-0000-4000-8000-000000000000"));
}

TEST(DefaultWelcome, NoPairOfCollidersStartsOverlapping)
{
  LoadedProject loaded;
  load(loaded, buildDefaultProject());
  const auto& objectManager = *loaded.sceneManager.getCurrentScene()->getObjectManager();

  std::vector<std::shared_ptr<Object>> withColliders;
  for (const auto& object : objectManager.getAllObjects())
  {
    if (object->getComponent<Collider>(ComponentType::collider))
    {
      withColliders.push_back(object);
    }
  }

  ASSERT_GT(withColliders.size(), 30u);

  size_t touching = 0;
  for (size_t i = 0; i < withColliders.size(); ++i)
  {
    for (size_t j = i + 1; j < withColliders.size(); ++j)
    {
      auto first = withColliders[i]->getComponent<Collider>(ComponentType::collider);
      auto second = withColliders[j]->getComponent<Collider>(ComponentType::collider);

      if (const auto contact = collisions::findContact(*first, *second))
      {
        ++touching;
        EXPECT_LT(contact->depth(), 0.01f) << withColliders[i]->getName() << " / " << withColliders[j]->getName();
      }
    }
  }

  // Resting contacts exist (the tower on the ground), so the check above ran on something.
  EXPECT_GT(touching, 0u);
}

TEST(DefaultWelcome, ItSettlesAndTheTowerStands)
{
  LoadedProject loaded;
  load(loaded, buildDefaultProject());
  const auto& objectManager = *loaded.sceneManager.getCurrentScene()->getObjectManager();

  const auto blocks = towerBlocks(objectManager);
  ASSERT_EQ(blocks.size(), 24u);

  std::vector<glm::vec3> startPositions;
  std::vector<glm::quat> startOrientations;
  for (const auto& block : blocks)
  {
    startPositions.push_back(fixtures::positionOf(block));
    startOrientations.push_back(fixtures::transformOf(block)->getOrientation());
  }

  CollisionSystem collisionSystem;
  for (int tick = 0; tick < 300; ++tick)
  {
    PhysicsSystem::fixedUpdate(objectManager, dt);
    collisionSystem.fixedUpdate(objectManager, dt);
  }

  for (const auto& object : objectManager.getAllObjects())
  {
    SCOPED_TRACE(object->getName());
    const auto position = fixtures::positionOf(object);
    EXPECT_TRUE(isFinite(position));
    EXPECT_GT(position.y, -50.0f);
  }

  for (size_t i = 0; i < blocks.size(); ++i)
  {
    SCOPED_TRACE(i);
    EXPECT_LT(glm::length(fixtures::positionOf(blocks[i]) - startPositions[i]), 0.05f);

    const float alignment = std::min(1.0f, std::abs(glm::dot(fixtures::transformOf(blocks[i])->getOrientation(),
                                                              startOrientations[i])));
    EXPECT_LT(glm::degrees(2.0f * std::acos(alignment)), 2.0f);
  }
}

TEST(DefaultWelcome, TheTowerBlocksAndShelfPropsTouchTheirSupports)
{
  LoadedProject loaded;
  load(loaded, buildDefaultProject());
  const auto& objectManager = *loaded.sceneManager.getCurrentScene()->getObjectManager();

  std::shared_ptr<Object> ground;
  std::shared_ptr<Object> shelf;
  for (const auto& object : objectManager.getAllObjects())
  {
    if (object->getName() == "Ground")
    {
      ground = object;
    }
    else if (object->getName() == "Shape Shelf")
    {
      shelf = object;
    }
  }

  ASSERT_NE(ground, nullptr);
  ASSERT_NE(shelf, nullptr);

  const auto bottomBlock = [&](const std::shared_ptr<Object>& object) {
    return fixtures::positionOf(object).y < 1.0f;
  };

  size_t grounded = 0;
  for (const auto& block : towerBlocks(objectManager))
  {
    if (!bottomBlock(block))
    {
      continue;
    }

    auto blockCollider = block->getComponent<Collider>(ComponentType::collider);
    auto groundCollider = ground->getComponent<Collider>(ComponentType::collider);
    const auto contact = collisions::findContact(*blockCollider, *groundCollider);
    EXPECT_LT(contact ? contact->depth() : 0.0f, 0.01f);
    EXPECT_NEAR(fixtures::positionOf(block).y, 0.5f, 1e-4f);
    ++grounded;
  }

  EXPECT_EQ(grounded, 4u);

  std::vector<std::shared_ptr<Object>> shelfProps;
  std::vector<float> startHeights;
  for (const auto& object : objectManager.getAllObjects())
  {
    const auto position = fixtures::positionOf(object);
    if (!object->getComponent<RigidBody>(ComponentType::rigidBody) || position.x < -19.0f || position.x > -15.0f
        || position.z < 17.5f || position.z > 26.5f)
    {
      continue;
    }

    SCOPED_TRACE(object->getName());
    auto shapeCollider = object->getComponent<Collider>(ComponentType::collider);
    auto shelfCollider = shelf->getComponent<Collider>(ComponentType::collider);
    const auto contact = collisions::findContact(*shapeCollider, *shelfCollider);
    EXPECT_LT(contact ? contact->depth() : 0.0f, 0.01f);
    shelfProps.push_back(object);
    startHeights.push_back(position.y);
  }

  ASSERT_EQ(shelfProps.size(), 9u);

  // A prop that started in the air would have dropped by now; one that started sunk would have been pushed up.
  CollisionSystem collisionSystem;
  for (int tick = 0; tick < 50; ++tick)
  {
    PhysicsSystem::fixedUpdate(objectManager, dt);
    collisionSystem.fixedUpdate(objectManager, dt);
  }

  for (size_t i = 0; i < shelfProps.size(); ++i)
  {
    SCOPED_TRACE(shelfProps[i]->getName());
    EXPECT_NEAR(fixtures::positionOf(shelfProps[i]).y, startHeights[i], 0.02f);
  }
}
