#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "DefaultProject.h"
#include "DefaultProjectParts.h"
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
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
  constexpr float dt = 1.0f / 50.0f;

  // The distance PhysicsSystem's gravity step moves a body in one tick (gravityStep in PhysicsSystem.cpp).
  constexpr float gravityStepPerTick = 9.81f * dt * 0.1f;

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

  std::shared_ptr<Object> objectNamed(const ObjectManager& objectManager, const std::string& name)
  {
    for (const auto& object : objectManager.getAllObjects())
    {
      if (object->getName() == name)
      {
        return object;
      }
    }

    return nullptr;
  }

  bool isOnShelf(const std::shared_ptr<Object>& object)
  {
    const auto position = fixtures::positionOf(object);

    return object->getComponent<RigidBody>(ComponentType::rigidBody) && position.x > -19.0f && position.x < -15.0f
      && position.z > 17.5f && position.z < 26.5f;
  }

  std::vector<std::shared_ptr<Object>> shelfProps(const ObjectManager& objectManager)
  {
    std::vector<std::shared_ptr<Object>> props;
    for (const auto& object : objectManager.getAllObjects())
    {
      if (isOnShelf(object))
      {
        props.push_back(object);
      }
    }

    return props;
  }

  // Zero when the pair does not touch, which is also what exactly touching colliders report.
  float contactDepth(const std::shared_ptr<Object>& object, const std::shared_ptr<Object>& other)
  {
    auto first = object->getComponent<Collider>(ComponentType::collider);
    auto second = other->getComponent<Collider>(ComponentType::collider);
    const auto contact = collisions::findContact(*first, *second);

    return contact ? contact->depth() : 0.0f;
  }

  void tick(const ObjectManager& objectManager, const int count)
  {
    CollisionSystem collisionSystem;
    for (int i = 0; i < count; ++i)
    {
      PhysicsSystem::fixedUpdate(objectManager, dt);
      collisionSystem.fixedUpdate(objectManager, dt);
    }
  }

  // The hull's volume centroid, from tetrahedra to its triangles.
  glm::vec3 centroidOf(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices)
  {
    glm::vec3 weighted(0.0f);
    float volume = 0.0f;
    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
      const auto& a = vertices[indices[i]];
      const auto& b = vertices[indices[i + 1]];
      const auto& c = vertices[indices[i + 2]];
      const float tetrahedron = glm::dot(a, glm::cross(b, c)) / 6.0f;
      weighted += tetrahedron * (a + b + c) / 4.0f;
      volume += tetrahedron;
    }

    return weighted / volume;
  }

  // Convex outline of the points, counter-clockwise (monotone chain).
  std::vector<glm::vec2> outline(std::vector<glm::vec2> points)
  {
    std::sort(points.begin(), points.end(), [](const glm::vec2& a, const glm::vec2& b) {
      return a.x < b.x || (a.x == b.x && a.y < b.y);
    });

    const auto turn = [](const glm::vec2& o, const glm::vec2& a, const glm::vec2& b) {
      return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    };

    std::vector<glm::vec2> hull;
    for (int pass = 0; pass < 2; ++pass)
    {
      const size_t start = hull.size();
      for (const auto& point : points)
      {
        while (hull.size() >= start + 2 && turn(hull[hull.size() - 2], hull.back(), point) <= 0.0f)
        {
          hull.pop_back();
        }

        hull.push_back(point);
      }

      hull.pop_back();
      std::reverse(points.begin(), points.end());
    }

    return hull;
  }

  // Distance from the centroid's footprint to the nearest edge of the resting face; negative when outside it.
  float restingMargin(const std::shared_ptr<Object>& prop, const ConvexHull& hull)
  {
    const auto transform = fixtures::transformOf(prop);
    const auto orientation = transform->getOrientation();
    const auto scale = transform->getScale();

    std::vector<glm::vec3> world;
    for (const auto& vertex : hull.vertices)
    {
      world.push_back(orientation * (vertex * scale));
    }

    const auto centroid = centroidOf(world, hull.indices);
    const float lowest = std::min_element(world.begin(), world.end(), [](const glm::vec3& a, const glm::vec3& b) {
      return a.y < b.y;
    })->y;

    std::vector<glm::vec2> face;
    for (const auto& vertex : world)
    {
      if (vertex.y < lowest + 1e-3f)
      {
        face.emplace_back(vertex.x, vertex.z);
      }
    }

    const auto polygon = outline(face);
    if (polygon.size() < 3)
    {
      return -1.0f;
    }

    float margin = std::numeric_limits<float>::max();
    for (size_t i = 0; i < polygon.size(); ++i)
    {
      const auto& p = polygon[i];
      const auto edge = polygon[(i + 1) % polygon.size()] - p;
      const glm::vec2 inward = glm::normalize(glm::vec2(-edge.y, edge.x));
      margin = std::min(margin, glm::dot(glm::vec2(centroid.x, centroid.z) - p, inward));
    }

    return margin;
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

  for (size_t i = 0; i < withColliders.size(); ++i)
  {
    for (size_t j = i + 1; j < withColliders.size(); ++j)
    {
      EXPECT_LT(contactDepth(withColliders[i], withColliders[j]), 0.01f)
        << withColliders[i]->getName() << " / " << withColliders[j]->getName();
    }
  }
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

  tick(objectManager, 300);

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
    const auto moved = fixtures::positionOf(blocks[i]) - startPositions[i];
    EXPECT_LT(glm::length(glm::vec2(moved.x, moved.z)), 0.05f);

    // Each box rests one gravity step low on the one below, so the allowance grows with the level.
    const float level = std::round(startPositions[i].y - 0.5f);
    EXPECT_LT(-moved.y, (level + 1.0f) * gravityStepPerTick + 0.01f);
    EXPECT_GT(moved.y, -1.0f);

    const float alignment = std::min(1.0f, std::abs(glm::dot(fixtures::transformOf(blocks[i])->getOrientation(),
                                                              startOrientations[i])));
    EXPECT_LT(glm::degrees(2.0f * std::acos(alignment)), 2.0f);
  }
}

TEST(DefaultWelcome, TheTowerBlocksTouchTheGround)
{
  LoadedProject loaded;
  load(loaded, buildDefaultProject());
  const auto& objectManager = *loaded.sceneManager.getCurrentScene()->getObjectManager();
  const auto ground = objectNamed(objectManager, "Ground");
  ASSERT_NE(ground, nullptr);

  size_t grounded = 0;
  for (const auto& block : towerBlocks(objectManager))
  {
    if (fixtures::positionOf(block).y < 1.0f)
    {
      EXPECT_LT(contactDepth(block, ground), 0.01f);
      EXPECT_NEAR(fixtures::positionOf(block).y, 0.5f, 1e-4f);
      ++grounded;
    }
  }

  EXPECT_EQ(grounded, 4u);
}

TEST(DefaultWelcome, TheShelfPropsTouchTheShelfAndStayPut)
{
  LoadedProject loaded;
  load(loaded, buildDefaultProject());
  const auto& objectManager = *loaded.sceneManager.getCurrentScene()->getObjectManager();
  const auto shelf = objectNamed(objectManager, "Shape Shelf");
  ASSERT_NE(shelf, nullptr);

  const auto props = shelfProps(objectManager);
  ASSERT_EQ(props.size(), 9u);

  std::vector<float> startHeights;
  for (const auto& prop : props)
  {
    EXPECT_LT(contactDepth(prop, shelf), 0.01f) << prop->getName();
    startHeights.push_back(fixtures::positionOf(prop).y);
  }

  // A prop that started in the air would have dropped by now; one that started sunk would have been pushed up.
  tick(objectManager, 50);

  for (size_t i = 0; i < props.size(); ++i)
  {
    EXPECT_NEAR(fixtures::positionOf(props[i]).y, startHeights[i], 0.02f) << props[i]->getName();
  }
}

TEST(DefaultWelcome, EveryShelfPropRestsWithItsCenterOfMassOverItsFace)
{
  LoadedProject loaded;
  load(loaded, buildDefaultProject());
  const auto& objectManager = *loaded.sceneManager.getCurrentScene()->getObjectManager();

  size_t checked = 0;
  for (const auto& prop : shelfProps(objectManager))
  {
    for (const auto& shape : defaultproject::libraryShapes())
    {
      if (shape.name == prop->getName())
      {
        // 0.05 at unit scale, and a prop is half size.
        EXPECT_GE(restingMargin(prop, shape.hull()), 0.025f - 1e-3f) << shape.name;
        ++checked;
      }
    }
  }

  EXPECT_EQ(checked, 9u);
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
