#include <gtest/gtest.h>

#include "DefaultProject.h"
#include "DefaultWelcomeTestHelpers.h"
#include "TestScene.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Transform.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"

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
#include <string>
#include <utility>
#include <vector>

using namespace welcometest;

namespace {
  // Class name -> exposed field name -> C# type name.
  using ClassFields = std::map<std::string, std::map<std::string, std::string>>;

  std::string fileText(const std::filesystem::path& path)
  {
    std::ifstream stream(path);

    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  }

  // Adds the classes of one file and the [ExposeToEditor] fields declared after each.
  void parseScriptFile(const std::string& text, ClassFields& classes)
  {
    const std::regex classPattern(R"(\bclass\s+(\w+))");
    const std::regex fieldPattern(
      R"(\[ExposeToEditor[^\]]*\]\s*(?:public|private|protected|internal)?\s*([\w<>]+)\s+(\w+))");

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
        classes[*owner][(*it)[2].str()] = (*it)[1].str();
      }
    }
  }

  ClassFields shippedScriptClasses()
  {
    const auto root = std::filesystem::path(ECS3D_SOURCE_DIR) / "source" / "libs" / "scripting" / "UserScripts";

    ClassFields classes;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
    {
      if (entry.path().extension() == ".cs")
      {
        parseScriptFile(fileText(entry.path()), classes);
      }
    }

    return classes;
  }

  // The wire type tag a C# field type is exposed as.
  std::string tagOf(const std::string& csType)
  {
    return csType == "Vector3" ? "vector3" : csType;
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

  size_t countNamed(const nlohmann::json& objects, const std::string& name)
  {
    size_t count = 0;
    for (const auto& object : objects)
    {
      count += object.at("name") == name ? 1 : 0;
      count += countNamed(object.at("children"), name);
    }

    return count;
  }

  std::vector<nlohmann::json> scriptsOf(const nlohmann::json& project)
  {
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

    return scripts;
  }

  void expectFieldMatches(const nlohmann::json& field, const std::map<std::string, std::string>& declared,
                          const std::set<std::string>& known)
  {
    const std::regex uuidPattern(R"([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})");
    const auto name = field.at("name").get<std::string>();
    SCOPED_TRACE(name);

    ASSERT_TRUE(declared.contains(name));
    EXPECT_EQ(field.at("type"), tagOf(declared.at(name)));

    if (field.at("value").is_string() && std::regex_match(field.at("value").get<std::string>(), uuidPattern))
    {
      EXPECT_TRUE(known.contains(field.at("value").get<std::string>())) << "not an asset or prefab uuid";
    }
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
  EXPECT_EQ(classes.at("Spinner").at("m_axis"), "Vector3");

  const auto project = buildDefaultProject();
  const auto known = assetUuids(project);
  const auto scripts = scriptsOf(project);

  // Positive control: the Welcome scene really uses scripts.
  EXPECT_GT(scripts.size(), 15u);

  for (const auto& script : scripts)
  {
    const auto className = script.at("className").get<std::string>();
    SCOPED_TRACE(className);
    ASSERT_TRUE(classes.contains(className));

    for (const auto& field : script.at("fields"))
    {
      expectFieldMatches(field, classes.at(className), known);
    }
  }
}

TEST(DefaultWelcome, ObjectsScriptsLookUpByNameExistOnce)
{
  const auto& objects = sceneNamed(buildDefaultProject(), "Welcome").at("objects");

  for (const char* name : { "Earth", "LaserMarker", "Sun" })
  {
    EXPECT_EQ(countNamed(objects, name), 1u) << name;
  }

  // Positive control for the counter: the tower has many blocks of one name.
  EXPECT_EQ(countNamed(objects, "Tower Block"), 24u);
}

TEST(DefaultWelcome, EveryAssetItReferencesIsRegistered)
{
  const auto project = buildDefaultProject();
  const auto known = assetUuids(project);

  std::vector<std::string> references;
  for (const auto& object : sceneNamed(project, "Welcome").at("objects"))
  {
    collectAssetReferences(object, references);
  }

  EXPECT_GT(references.size(), 100u);
  for (const auto& reference : references)
  {
    EXPECT_TRUE(known.contains(reference)) << reference;
  }

  // Positive control for the lookup.
  EXPECT_FALSE(known.contains("00000000-0000-4000-8000-000000000000"));
}

TEST(DefaultWelcome, TheGroundIsSixteenTilesCoveringTheFloor)
{
  LoadedProject loaded;
  load(loaded, buildDefaultProject());
  const auto ground = objectNamed(*loaded.sceneManager.getCurrentScene()->getObjectManager(), "Ground");
  ASSERT_NE(ground, nullptr);
  ASSERT_EQ(ground->getChildren().size(), 16u);

  std::set<std::pair<float, float>> centers;
  float low = 1000.0f;
  float high = -1000.0f;
  for (const auto& tile : ground->getChildren())
  {
    const auto transform = fixtures::transformOf(tile);
    fixtures::expectNear("tile scale", transform->getScale(), glm::vec3(10.0f, 1.0f, 10.0f), 1e-4f);

    const auto position = transform->getPosition();
    EXPECT_NEAR(position.y, 0.0f, 1e-4f);
    centers.insert({ std::round(position.x), std::round(position.z) });
    low = std::min({ low, position.x - 10.0f, position.z - 10.0f });
    high = std::max({ high, position.x + 10.0f, position.z + 10.0f });
  }

  EXPECT_EQ(centers.size(), 16u);
  EXPECT_NEAR(low, -40.0f, 1e-3f);
  EXPECT_NEAR(high, 40.0f, 1e-3f);
}
