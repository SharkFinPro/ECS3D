#include <gtest/gtest.h>

#include "TestScene.h"
#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "Replication.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"

#include <glm/vec3.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <uuid.h>

// Integration coverage for the data-layer half of prefab instantiation: registering a body, cloning it
// into a scene through the instantiatePrefab sceneEdit op, and the update-in-place/detached-instance
// contract AGENTS.md describes. Per-instance overrides and prefab->instance propagation don't exist yet
// (deliberately deferred), and TransientObject lives in the editor library this suite cannot link, so the
// editing-a-prefab-in-the-inspector half is covered only as far as the data calls it makes.
namespace {
  using replication::SceneEditResult;

  uuids::uuid uuidFrom(const std::string& text)
  {
    return uuids::uuid::from_string(text).value();
  }

  const auto prefabUUID = uuidFrom("55555555-5555-5555-5555-555555555555");
  const auto otherAssetUUID = uuidFrom("66666666-6666-6666-6666-666666666666");

  // Build a prefab body the way the editor does: author a live object (with a child) through the same
  // fixtures every other suite uses, then take its Object::serialize() blob. A throwaway ObjectManager
  // backs the authoring, never touched again once the body is taken.
  nlohmann::json makeBody(const std::string& name, const glm::vec3& position, const glm::vec3& scale)
  {
    fixtures::Scene authoring;
    const auto root = fixtures::addObject(authoring, name, position, scale);
    fixtures::addBoxCollider(root);
    fixtures::addRigidBody(root);

    fixtures::addChildObject(authoring, name + " Child", root);

    return root->serialize();
  }

  void addUUIDs(const nlohmann::json& node, std::unordered_set<uuids::uuid>& out)
  {
    out.insert(uuidFrom(std::string(node.at("uuid"))));

    for (const auto& child : node.at("children"))
    {
      addUUIDs(child, out);
    }
  }

  void addUUIDs(const std::shared_ptr<Object>& object, std::unordered_set<uuids::uuid>& out)
  {
    out.insert(object->getUUID());

    for (const auto& child : object->getChildren())
    {
      addUUIDs(child, out);
    }
  }

  glm::vec3 positionInBody(const nlohmann::json& body)
  {
    for (const auto& component : body.at("components"))
    {
      if (component.at("type") == "Transform")
      {
        const auto& position = component.at("position");
        return { position.at(0).get<float>(), position.at(1).get<float>(), position.at(2).get<float>() };
      }
    }

    throw std::runtime_error("body has no Transform");
  }

  // Every test here instantiates the same registered prefab; this is the applySceneEdit call every one of
  // them makes, minus the boilerplate.
  SceneEditResult instantiatePrefab(const fixtures::Scene& scene, const AssetRegistry& registry)
  {
    return replication::applySceneEdit(*scene.objectManager, replication::buildInstantiatePrefab(prefabUUID),
                                       &registry);
  }
}

TEST(Prefab, InstantiatingBuildsTheWholeSubtreeFromTheRegisteredBody)
{
  const auto scene = fixtures::makeScene();

  AssetRegistry registry;
  registry.registerAsset({ .uuid = prefabUUID, .type = AssetType::Prefab, .path = "Block",
                           .body = makeBody("Block", glm::vec3(1, 2, 3), glm::vec3(4)).dump() });

  ASSERT_EQ(instantiatePrefab(scene, registry), SceneEditResult::applied);

  ASSERT_EQ(scene.objectManager->getObjects().size(), 1u);
  const auto instance = scene.objectManager->getObjects().front();

  // instantiate lands the whole subtree at the scene root - there is no target-parent field on the op.
  EXPECT_EQ(instance->getParent(), nullptr);
  EXPECT_EQ(instance->getName(), "Block");
  fixtures::expectNear(fixtures::positionOf(instance), glm::vec3(1, 2, 3));
  fixtures::expectNear(fixtures::transformOf(instance)->getScale(), glm::vec3(4));

  const auto rigidBody = instance->getComponent<RigidBody>(ComponentType::rigidBody);
  ASSERT_NE(rigidBody, nullptr);
  EXPECT_FLOAT_EQ(rigidBody->getMass(), 10.0f);
  EXPECT_FLOAT_EQ(rigidBody->getFriction(), 0.1f);

  ASSERT_NE(instance->getComponent<BoxCollider>(ComponentType::collider), nullptr);

  ASSERT_EQ(instance->getChildren().size(), 1u);
  EXPECT_EQ(instance->getChildren().front()->getName(), "Block Child");
}

TEST(Prefab, EveryInstantiationGetsFreshUuidsForTheWholeSubtree)
{
  const auto scene = fixtures::makeScene();

  const auto body = makeBody("Block", glm::vec3(0), glm::vec3(1));
  AssetRegistry registry;
  registry.registerAsset({ .uuid = prefabUUID, .type = AssetType::Prefab, .path = "Block", .body = body.dump() });

  std::unordered_set<uuids::uuid> bodyUUIDs;
  addUUIDs(body, bodyUUIDs);
  ASSERT_EQ(bodyUUIDs.size(), 2u); // root + child, so a collision below would actually be caught

  ASSERT_EQ(instantiatePrefab(scene, registry), SceneEditResult::applied);
  ASSERT_EQ(instantiatePrefab(scene, registry), SceneEditResult::applied);

  ASSERT_EQ(scene.objectManager->getObjects().size(), 2u);

  std::unordered_set<uuids::uuid> firstInstanceUUIDs;
  addUUIDs(scene.objectManager->getObjects()[0], firstInstanceUUIDs);

  std::unordered_set<uuids::uuid> secondInstanceUUIDs;
  addUUIDs(scene.objectManager->getObjects()[1], secondInstanceUUIDs);

  ASSERT_EQ(firstInstanceUUIDs.size(), 2u);
  ASSERT_EQ(secondInstanceUUIDs.size(), 2u);

  for (const auto& id : firstInstanceUUIDs)
  {
    EXPECT_EQ(bodyUUIDs.count(id), 0u);
    EXPECT_EQ(secondInstanceUUIDs.count(id), 0u);
  }

  for (const auto& id : secondInstanceUUIDs)
  {
    EXPECT_EQ(bodyUUIDs.count(id), 0u);
  }
}

TEST(Prefab, ReRegisteringThePrefabLeavesAnAlreadyPlacedInstanceUntouched)
{
  const auto scene = fixtures::makeScene();

  AssetRegistry registry;
  registry.registerAsset({ .uuid = prefabUUID, .type = AssetType::Prefab, .path = "Block",
                           .body = makeBody("Block", glm::vec3(1, 0, 0), glm::vec3(1)).dump() });

  ASSERT_EQ(instantiatePrefab(scene, registry), SceneEditResult::applied);
  const auto placed = scene.objectManager->getObjects().front();

  // "Save as Prefab" over the existing name: updates the body in place and keeps the original uuid, even
  // though this record names a different one.
  registry.registerAsset({ .uuid = otherAssetUUID, .type = AssetType::Prefab, .path = "Block",
                           .body = makeBody("Block V2", glm::vec3(9, 9, 9), glm::vec3(1)).dump() });

  ASSERT_NE(registry.getByUUID(prefabUUID), nullptr);
  EXPECT_EQ(registry.getByUUID(otherAssetUUID), nullptr);

  // Instances are detached copies - nothing ties this object back to the asset, so the update cannot
  // reach it.
  EXPECT_EQ(placed->getName(), "Block");
  fixtures::expectNear(fixtures::positionOf(placed), glm::vec3(1, 0, 0));

  // Positive control: a fresh instantiation after the update does pick up the new body, proving the
  // update itself took and the assertions above are not just passing against dead machinery.
  ASSERT_EQ(instantiatePrefab(scene, registry), SceneEditResult::applied);
  ASSERT_EQ(scene.objectManager->getObjects().size(), 2u);

  const auto updated = scene.objectManager->getObjects().back();
  EXPECT_EQ(updated->getName(), "Block V2");
  fixtures::expectNear(fixtures::positionOf(updated), glm::vec3(9, 9, 9));
}

TEST(Prefab, MutatingAPlacedInstanceDoesNotAlterTheRegistrysStoredBody)
{
  const auto scene = fixtures::makeScene();

  AssetRegistry registry;
  registry.registerAsset({ .uuid = prefabUUID, .type = AssetType::Prefab, .path = "Block",
                           .body = makeBody("Block", glm::vec3(0), glm::vec3(1)).dump() });

  ASSERT_EQ(instantiatePrefab(scene, registry), SceneEditResult::applied);
  const auto placed = scene.objectManager->getObjects().front();

  placed->setName("Renamed In Scene");
  fixtures::transformOf(placed)->setPosition(glm::vec3(9, 9, 9));

  const auto storedBody = registry.getPrefabBody(prefabUUID);
  EXPECT_EQ(storedBody.at("name"), "Block");
  fixtures::expectNear(positionInBody(storedBody), glm::vec3(0));
}

TEST(Prefab, TheStoredBodyRoundTripsThroughAScratchObjectManagerPreservingUuids)
{
  const auto componentRegistry = std::make_shared<ComponentRegistry>();
  registerDataComponents(*componentRegistry);

  AssetRegistry registry;
  registry.registerAsset({ .uuid = prefabUUID, .type = AssetType::Prefab, .path = "Block",
                           .body = makeBody("Block", glm::vec3(3, 4, 5), glm::vec3(2)).dump() });

  const auto parsed = registry.getPrefabBody(prefabUUID);
  ASSERT_TRUE(parsed.is_object());

  // The data half of TransientObject's edit path (source/libs/editor/TransientObject.cpp): a private
  // scratch manager, an Object built straight from the parsed body with its uuids intact (no
  // reassignUUIDs, unlike instantiate), children loaded separately since the ctor only reads the root's
  // own components/scripts.
  auto scratchManager = std::make_unique<ObjectManager>(componentRegistry);
  auto scratchObject = std::make_shared<Object>(parsed, scratchManager.get());
  scratchManager->addObject(scratchObject);
  scratchObject->loadChildren(parsed.at("children"));

  EXPECT_EQ(uuids::to_string(scratchObject->getUUID()), std::string(parsed.at("uuid")));
  ASSERT_EQ(scratchObject->getChildren().size(), 1u);
  EXPECT_EQ(uuids::to_string(scratchObject->getChildren().front()->getUUID()),
            std::string(parsed.at("children").at(0).at("uuid")));

  // Re-serialize and re-register under the same name - the same "Save as Prefab" path a real edit takes.
  // The asset uuid has to survive (this record names a different one) and the body it hands back has to
  // match what was just serialized.
  const auto reserialized = scratchObject->serialize();
  registry.registerAsset({ .uuid = otherAssetUUID, .type = AssetType::Prefab, .path = "Block",
                           .body = reserialized.dump() });

  ASSERT_EQ(registry.getAssets().size(), 1u);
  ASSERT_NE(registry.getByUUID(prefabUUID), nullptr);
  EXPECT_EQ(registry.getByUUID(otherAssetUUID), nullptr);
  EXPECT_EQ(registry.getPrefabBody(prefabUUID), reserialized);
}
