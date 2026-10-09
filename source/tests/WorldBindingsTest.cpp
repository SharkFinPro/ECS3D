#include <gtest/gtest.h>

#include "ObjectManagerFixtures.h"
#include "TestScene.h"

#include "BindingContext.h"
#include "WorldBindings.h"

#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Transform.h"

#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <uuid.h>
#include <vector>

namespace {
  using objectManagerFixtures::unknownUUID;

  struct RaycastCall
  {
    int calls = 0;
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f};
    float maxDistance = 0.0f;
    uint32_t layerMask = 0;
    uuids::uuid ignore;
  };

  struct OverlapCall
  {
    int calls = 0;
    glm::vec3 center{0.0f};
    float radius = 0.0f;
    uint32_t layerMask = 0;
    uuids::uuid ignore;
  };

  // The injected queries are plain function pointers, so what they see has to live at namespace scope.
  RaycastCall g_raycast;
  OverlapCall g_overlap;
  bool g_raycastHits = true;
  uuids::uuid g_hitObject;
  std::vector<uuids::uuid> g_overlapResults;

  const BindingContext::RaycastFn fakeRaycast = [](ObjectManager&, const glm::vec3& origin,
                                                   const glm::vec3& direction, const float maxDistance,
                                                   const uint32_t layerMask, const uuids::uuid& ignore,
                                                   uuids::uuid& hitObject, glm::vec3& hitPoint,
                                                   glm::vec3& hitNormal, float& hitDistance) -> bool
  {
    ++g_raycast.calls;
    g_raycast.origin = origin;
    g_raycast.direction = direction;
    g_raycast.maxDistance = maxDistance;
    g_raycast.layerMask = layerMask;
    g_raycast.ignore = ignore;

    if (!g_raycastHits)
    {
      return false;
    }

    hitObject = g_hitObject;
    hitPoint = glm::vec3(1.5f, 2.5f, 3.5f);
    hitNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    hitDistance = 4.25f;
    return true;
  };

  void fakeOverlapSphere(ObjectManager&, const glm::vec3& center, const float radius, const uint32_t layerMask,
                         const uuids::uuid& ignore, std::vector<uuids::uuid>& results)
  {
    ++g_overlap.calls;
    g_overlap.center = center;
    g_overlap.radius = radius;
    g_overlap.layerMask = layerMask;
    g_overlap.ignore = ignore;
    results = g_overlapResults;
  }

  std::vector<std::string> splitCommas(const std::string& text)
  {
    std::vector<std::string> parts;
    std::stringstream stream(text);
    std::string part;
    while (std::getline(stream, part, ','))
    {
      parts.push_back(part);
    }
    return parts;
  }

  // The binding hands back a pointer into a buffer the next call overwrites, so every result is copied
  // out at once, the way the managed side marshals it.
  std::string copyOut(const char* result)
  {
    return std::string(result);
  }

  std::string uuidOf(const std::shared_ptr<Object>& object)
  {
    return uuids::to_string(object->getUUID());
  }

  class WorldBindingsTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
      scene = fixtures::makeScene();
      BindingContext::setObjectManager(scene.objectManager.get());
      g_raycast = {};
      g_overlap = {};
      g_raycastHits = true;
      g_overlapResults.clear();
    }

    void TearDown() override
    {
      BindingContext::setObjectManager(nullptr);
      BindingContext::setAssetRegistry(nullptr);
      BindingContext::setRaycast(nullptr);
      BindingContext::setOverlapSphere(nullptr);
      BindingContext::takeSpawned();
      BindingContext::takeDestroyed();
      BindingContext::takeComponentEdits();
    }

    fixtures::Scene scene;
    AssetRegistry registry;
    WorldBindings bindings = WorldBindingsProvider::getBindings();
  };

  // --- Lookup ----------------------------------------------------------------------------------------

  TEST_F(WorldBindingsTest, FindObjectByNameAndListAllSeeLiveObjects)
  {
    const auto first = fixtures::addObject(scene, "First");
    const auto second = fixtures::addObject(scene, "Second");

    EXPECT_EQ(copyOut(bindings.findObjectByName("Second")), uuidOf(second));
    EXPECT_EQ(copyOut(bindings.findObjectByName("First")), uuidOf(first));
    EXPECT_EQ(copyOut(bindings.findObjectByName("Missing")), "");

    const auto listed = splitCommas(copyOut(bindings.getAllObjectUuids()));
    ASSERT_EQ(listed.size(), 2u);
    EXPECT_NE(std::ranges::find(listed, uuidOf(first)), listed.end());
    EXPECT_NE(std::ranges::find(listed, uuidOf(second)), listed.end());
  }

  TEST_F(WorldBindingsTest, LookupsSeeAnObjectStillPendingInAScriptPass)
  {
    const auto original = fixtures::addObject(scene, "Original");

    const ObjectManager::ScriptPassGuard guard(*scene.objectManager);
    const auto spawned = std::make_shared<Object>("Pending");
    scene.objectManager->addObject(spawned);

    // Not in getAllObjects() yet, which is what makes the pending lookup the thing under test.
    ASSERT_EQ(scene.objectManager->getAllObjects().size(), 1u);
    ASSERT_EQ(scene.objectManager->getPendingAdditions().size(), 1u);

    EXPECT_EQ(copyOut(bindings.findObjectByName("Pending")), uuidOf(spawned));
    EXPECT_EQ(copyOut(bindings.findObjectByName("Original")), uuidOf(original));

    const auto listed = splitCommas(copyOut(bindings.getAllObjectUuids()));
    ASSERT_EQ(listed.size(), 2u);
    EXPECT_NE(std::ranges::find(listed, uuidOf(spawned)), listed.end());
    EXPECT_NE(std::ranges::find(listed, uuidOf(original)), listed.end());

    EXPECT_TRUE(bindings.objectExists(uuidOf(spawned).c_str()));
  }

  TEST_F(WorldBindingsTest, ObjectExistsAndNameForLiveUnknownAndMalformedUuids)
  {
    const auto object = fixtures::addObject(scene, "Named");
    const auto live = uuidOf(object);
    const auto unknown = uuids::to_string(unknownUUID());

    EXPECT_TRUE(bindings.objectExists(live.c_str()));
    EXPECT_EQ(copyOut(bindings.getObjectName(live.c_str())), "Named");

    EXPECT_FALSE(bindings.objectExists(unknown.c_str()));
    EXPECT_EQ(copyOut(bindings.getObjectName(unknown.c_str())), "");

    EXPECT_FALSE(bindings.objectExists("not-a-uuid"));
    EXPECT_EQ(copyOut(bindings.getObjectName("not-a-uuid")), "");

    EXPECT_FALSE(bindings.objectExists(nullptr));
    EXPECT_EQ(copyOut(bindings.getObjectName(nullptr)), "");
  }

  TEST_F(WorldBindingsTest, WithoutAnObjectManagerQueriesReturnNothing)
  {
    const auto object = fixtures::addObject(scene, "Named");
    const auto live = uuidOf(object);
    ASSERT_TRUE(bindings.objectExists(live.c_str()));

    BindingContext::setObjectManager(nullptr);

    EXPECT_FALSE(bindings.objectExists(live.c_str()));
    EXPECT_EQ(copyOut(bindings.findObjectByName("Named")), "");
    EXPECT_EQ(copyOut(bindings.getAllObjectUuids()), "");
    EXPECT_EQ(copyOut(bindings.spawnObject("X", 0, 0, 0)), "");
  }

  // --- Spawn / destroy -------------------------------------------------------------------------------

  TEST_F(WorldBindingsTest, SpawnObjectCreatesAPositionedObjectAndRecordsIt)
  {
    const auto uuidText = copyOut(bindings.spawnObject("Spawned", 1.0f, 2.0f, 3.0f));

    const auto parsed = uuids::uuid::from_string(uuidText);
    ASSERT_TRUE(parsed.has_value());
    const auto object = scene.objectManager->getObjectByUUID(parsed.value());
    ASSERT_NE(object, nullptr);
    EXPECT_EQ(object->getName(), "Spawned");
    fixtures::expectNear(fixtures::positionOf(object), glm::vec3(1, 2, 3));

    const auto spawned = BindingContext::takeSpawned();
    ASSERT_EQ(spawned.size(), 1u);
    EXPECT_EQ(spawned[0], object);
    EXPECT_TRUE(BindingContext::takeSpawned().empty());
  }

  TEST_F(WorldBindingsTest, SpawnObjectWithoutANameUsesTheDefaultName)
  {
    const auto parsed = uuids::uuid::from_string(copyOut(bindings.spawnObject(nullptr, 0.0f, 0.0f, 0.0f)));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(scene.objectManager->getObjectByUUID(parsed.value())->getName(), "Object");
  }

  TEST_F(WorldBindingsTest, SpawnPrefabInstantiatesTheBodyWithFreshUuidsAndRecordsIt)
  {
    const auto source = fixtures::addObject(scene, "PrefabSource", glm::vec3(9, 9, 9));
    const auto sourceChild = fixtures::addChildObject(scene, "PrefabChild", source);

    const auto prefabUUID = scene.objectManager->createUUID();
    registry.registerAsset({ prefabUUID, AssetType::Prefab, "Thing", "", source->serialize().dump(), "" });
    BindingContext::setAssetRegistry(&registry);

    const auto uuidText = copyOut(bindings.spawnPrefab(uuids::to_string(prefabUUID).c_str(), 4.0f, 5.0f, 6.0f));

    const auto parsed = uuids::uuid::from_string(uuidText);
    ASSERT_TRUE(parsed.has_value());
    const auto instance = scene.objectManager->getObjectByUUID(parsed.value());
    ASSERT_NE(instance, nullptr);
    EXPECT_EQ(instance->getName(), "PrefabSource");
    EXPECT_NE(instance, source);
    fixtures::expectNear(fixtures::positionOf(instance), glm::vec3(4, 5, 6));

    ASSERT_EQ(instance->getChildren().size(), 1u);
    EXPECT_EQ(instance->getChildren().front()->getName(), "PrefabChild");
    EXPECT_NE(instance->getChildren().front(), sourceChild);

    std::vector<uuids::uuid> instanceUUIDs;
    objectManagerFixtures::collectUUIDs(instance, instanceUUIDs);
    std::vector<uuids::uuid> sourceUUIDs;
    objectManagerFixtures::collectUUIDs(source, sourceUUIDs);
    EXPECT_TRUE(objectManagerFixtures::disjointUUIDs(instanceUUIDs, sourceUUIDs));

    const auto spawned = BindingContext::takeSpawned();
    ASSERT_EQ(spawned.size(), 1u);
    EXPECT_EQ(spawned[0], instance);

    fixtures::expectNear(fixtures::positionOf(source), glm::vec3(9, 9, 9));
  }

  TEST_F(WorldBindingsTest, SpawnPrefabRefusesWhatIsNotAUsablePrefab)
  {
    const auto source = fixtures::addObject(scene, "PrefabSource");
    const auto prefabUUID = scene.objectManager->createUUID();
    const auto modelUUID = scene.objectManager->createUUID();
    registry.registerAsset({ prefabUUID, AssetType::Prefab, "Thing", "", source->serialize().dump(), "" });
    registry.registerAsset({ modelUUID, AssetType::Model, "models/a.obj", "", "", "" });

    const auto prefabText = uuids::to_string(prefabUUID);

    // No registry injected yet: a valid prefab uuid still yields nothing.
    EXPECT_EQ(copyOut(bindings.spawnPrefab(prefabText.c_str(), 0, 0, 0)), "");
    EXPECT_TRUE(BindingContext::takeSpawned().empty());

    BindingContext::setAssetRegistry(&registry);

    EXPECT_EQ(copyOut(bindings.spawnPrefab(uuids::to_string(unknownUUID()).c_str(), 0, 0, 0)), "");
    EXPECT_EQ(copyOut(bindings.spawnPrefab(uuids::to_string(modelUUID).c_str(), 0, 0, 0)), "");
    EXPECT_EQ(copyOut(bindings.spawnPrefab("not-a-uuid", 0, 0, 0)), "");
    EXPECT_EQ(copyOut(bindings.spawnPrefab(nullptr, 0, 0, 0)), "");
    EXPECT_TRUE(BindingContext::takeSpawned().empty());

    // Positive control: with the registry set, the valid prefab does spawn.
    EXPECT_NE(copyOut(bindings.spawnPrefab(prefabText.c_str(), 0, 0, 0)), "");
    EXPECT_EQ(BindingContext::takeSpawned().size(), 1u);
  }

  TEST_F(WorldBindingsTest, SpawnPrefabWithAnUnknownComponentSpawnsNothing)
  {
    const auto source = fixtures::addObject(scene, "PrefabSource");
    auto body = source->serialize();
    body["components"].push_back({ { "type", "NoSuchComponent" } });

    const auto prefabUUID = scene.objectManager->createUUID();
    registry.registerAsset({ prefabUUID, AssetType::Prefab, "Broken", "", body.dump(), "" });
    BindingContext::setAssetRegistry(&registry);

    const auto before = scene.objectManager->getAllObjects().size();
    EXPECT_EQ(copyOut(bindings.spawnPrefab(uuids::to_string(prefabUUID).c_str(), 0, 0, 0)), "");
    EXPECT_TRUE(BindingContext::takeSpawned().empty());
    EXPECT_EQ(scene.objectManager->getAllObjects().size(), before);
  }

  TEST_F(WorldBindingsTest, DestroyObjectMarksForDeletionAndRecordsOnce)
  {
    const auto object = fixtures::addObject(scene, "Doomed");
    const auto other = fixtures::addObject(scene, "Other");
    const auto uuid = uuidOf(object);

    bindings.destroyObject(uuid.c_str());
    bindings.destroyObject(uuid.c_str());

    EXPECT_TRUE(scene.objectManager->isMarkedForDeletion(object));
    EXPECT_FALSE(scene.objectManager->isMarkedForDeletion(other));

    const auto destroyed = BindingContext::takeDestroyed();
    ASSERT_EQ(destroyed.size(), 1u);
    EXPECT_EQ(destroyed[0], object->getUUID());
  }

  TEST_F(WorldBindingsTest, DestroyObjectIgnoresUnknownAndMalformedUuids)
  {
    const auto object = fixtures::addObject(scene, "Survivor");

    bindings.destroyObject(uuids::to_string(unknownUUID()).c_str());
    bindings.destroyObject("not-a-uuid");
    bindings.destroyObject(nullptr);
    EXPECT_TRUE(BindingContext::takeDestroyed().empty());
    EXPECT_FALSE(scene.objectManager->isMarkedForDeletion(object));

    // Positive control: the same call with the live uuid does record.
    bindings.destroyObject(uuidOf(object).c_str());
    EXPECT_EQ(BindingContext::takeDestroyed().size(), 1u);
  }

  // --- Queries ---------------------------------------------------------------------------------------

  TEST_F(WorldBindingsTest, RaycastPassesItsArgumentsAndFormatsTheHit)
  {
    const auto target = fixtures::addObject(scene, "Target");
    const auto ignored = fixtures::addObject(scene, "Ignored");
    g_hitObject = target->getUUID();
    BindingContext::setRaycast(fakeRaycast);

    const auto result = copyOut(bindings.raycast(1.0f, 2.0f, 3.0f, 0.0f, -1.0f, 0.0f, 50.0f, 0x5u,
                                                 uuidOf(ignored).c_str()));

    ASSERT_EQ(g_raycast.calls, 1);
    fixtures::expectNear("origin", g_raycast.origin, glm::vec3(1, 2, 3));
    fixtures::expectNear("direction", g_raycast.direction, glm::vec3(0, -1, 0));
    EXPECT_FLOAT_EQ(g_raycast.maxDistance, 50.0f);
    EXPECT_EQ(g_raycast.layerMask, 0x5u);
    EXPECT_EQ(g_raycast.ignore, ignored->getUUID());

    const auto parts = splitCommas(result);
    ASSERT_EQ(parts.size(), 8u);
    EXPECT_EQ(parts[0], uuidOf(target));
    const float expected[] = { 4.25f, 1.5f, 2.5f, 3.5f, 0.0f, 1.0f, 0.0f };
    for (std::size_t i = 0; i < 7; ++i)
    {
      EXPECT_NEAR(std::stof(parts[i + 1]), expected[i], 1e-5f) << "field " << i + 1;
    }
  }

  TEST_F(WorldBindingsTest, RaycastMissAndBadIgnoreArgument)
  {
    BindingContext::setRaycast(fakeRaycast);

    g_raycastHits = false;
    EXPECT_EQ(copyOut(bindings.raycast(0, 0, 0, 0, 0, 1, 10.0f, 1u, nullptr)), "");
    EXPECT_EQ(g_raycast.calls, 1);
    EXPECT_TRUE(g_raycast.ignore.is_nil());

    // A malformed ignore uuid means "ignore nothing", and the query still runs.
    g_raycastHits = true;
    g_hitObject = unknownUUID();
    EXPECT_NE(copyOut(bindings.raycast(0, 0, 0, 0, 0, 1, 10.0f, 1u, "garbage")), "");
    EXPECT_EQ(g_raycast.calls, 2);
    EXPECT_TRUE(g_raycast.ignore.is_nil());
  }

  TEST_F(WorldBindingsTest, RaycastAndOverlapWithNothingInjectedReturnAMiss)
  {
    g_raycastHits = true;
    g_hitObject = unknownUUID();
    g_overlapResults = { unknownUUID() };

    EXPECT_EQ(copyOut(bindings.raycast(0, 0, 0, 0, 0, 1, 10.0f, 1u, nullptr)), "");
    EXPECT_EQ(copyOut(bindings.overlapSphere(0, 0, 0, 1.0f, 1u, nullptr)), "");
    EXPECT_EQ(g_raycast.calls, 0);
    EXPECT_EQ(g_overlap.calls, 0);

    // Positive control: once injected, the same calls reach the functions and report results.
    BindingContext::setRaycast(fakeRaycast);
    BindingContext::setOverlapSphere(&fakeOverlapSphere);
    EXPECT_NE(copyOut(bindings.raycast(0, 0, 0, 0, 0, 1, 10.0f, 1u, nullptr)), "");
    EXPECT_NE(copyOut(bindings.overlapSphere(0, 0, 0, 1.0f, 1u, nullptr)), "");
  }

  TEST_F(WorldBindingsTest, OverlapSpherePassesItsArgumentsAndListsTheResults)
  {
    const auto ignored = fixtures::addObject(scene, "Ignored");
    const auto a = scene.objectManager->createUUID();
    const auto b = scene.objectManager->createUUID();
    g_overlapResults = { a, b };
    BindingContext::setOverlapSphere(&fakeOverlapSphere);

    const auto result = copyOut(bindings.overlapSphere(1.0f, 2.0f, 3.0f, 4.5f, 0x3u, uuidOf(ignored).c_str()));

    ASSERT_EQ(g_overlap.calls, 1);
    fixtures::expectNear("center", g_overlap.center, glm::vec3(1, 2, 3));
    EXPECT_FLOAT_EQ(g_overlap.radius, 4.5f);
    EXPECT_EQ(g_overlap.layerMask, 0x3u);
    EXPECT_EQ(g_overlap.ignore, ignored->getUUID());
    EXPECT_EQ(result, uuids::to_string(a) + "," + uuids::to_string(b));

    g_overlapResults.clear();
    EXPECT_EQ(copyOut(bindings.overlapSphere(0, 0, 0, 1.0f, 1u, nullptr)), "");
    EXPECT_EQ(g_overlap.calls, 2);
  }
}
