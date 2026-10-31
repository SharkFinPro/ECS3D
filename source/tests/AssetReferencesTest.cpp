#include <gtest/gtest.h>

#include <AssetReferences.h>
#include <ComponentRegistration.h>
#include <ComponentRegistry.h>
#include <assets/AssetRegistry.h>
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/ModelRenderer.h>
#include <objects/components/Script.h>
#include <objects/components/collisions/ConvexMeshCollider.h>
#include <scenes/SceneAsset.h>
#include <scenes/SceneManager.h>

#include <nlohmann/json.hpp>

#include <memory>
#include <string>

namespace {
  uuids::uuid uuidFrom(const std::string& text)
  {
    return uuids::uuid::from_string(text).value();
  }

  const auto modelUUID = uuidFrom("22222222-2222-2222-2222-222222222222");
  const auto textureUUID = uuidFrom("33333333-3333-3333-3333-333333333333");
  const auto unusedUUID = uuidFrom("99999999-9999-9999-9999-999999999999");

  std::shared_ptr<Object> makeModelObject(const std::string& name, const uuids::uuid& model,
                                          const uuids::uuid& texture = uuids::uuid())
  {
    auto object = std::make_shared<Object>(name);

    auto renderer = std::make_shared<ModelRenderer>();
    renderer->setModelUUID(model);
    renderer->setTextureUUID(texture);
    object->addComponent(renderer);

    return object;
  }

  void addScriptWithAssetField(const std::shared_ptr<Object>& object, const uuids::uuid& asset)
  {
    auto script = std::make_shared<Script>("Spawner");
    script->setFields(nlohmann::json::array({
      { { "name", "speed" }, { "type", "float" }, { "value", 2.0 } },
      { { "name", "model" }, { "type", "string" }, { "asset", "model" }, { "value", uuids::to_string(asset) } }
    }));
    object->addComponent(script);
  }

  AssetRecord prefabRecord(const std::string& name, const std::string& body)
  {
    AssetRecord record;
    record.uuid = uuidFrom("44444444-4444-4444-4444-444444444444");
    record.type = AssetType::Prefab;
    record.path = name;
    record.body = body;
    return record;
  }

  class AssetReferencesTest : public testing::Test
  {
  protected:
    std::shared_ptr<ComponentRegistry> m_componentRegistry = std::make_shared<ComponentRegistry>();
    SceneManager m_sceneManager;
    AssetRegistry m_assetRegistry;

    void SetUp() override
    {
      registerDataComponents(*m_componentRegistry);
    }

    std::shared_ptr<SceneAsset> addScene(const std::string& uuid, const std::string& name)
    {
      auto scene = std::make_shared<SceneAsset>(uuidFrom(uuid), name, m_componentRegistry);
      m_sceneManager.addScene(scene);
      return scene;
    }

    [[nodiscard]] int count(const uuids::uuid& assetUUID) const
    {
      return countAssetReferences(m_sceneManager, m_assetRegistry, assetUUID);
    }
  };
}

TEST_F(AssetReferencesTest, CountsAnObjectReferencingTheAssetAndNotAnUnreferencedUuid)
{
  const auto scene = addScene("11111111-1111-1111-1111-111111111111", "Scene");
  scene->getObjectManager()->addObject(makeModelObject("Cube", modelUUID));

  EXPECT_EQ(count(modelUUID), 1);
  EXPECT_EQ(count(unusedUUID), 0);
}

TEST_F(AssetReferencesTest, CountsATextureReferenceToo)
{
  const auto scene = addScene("11111111-1111-1111-1111-111111111111", "Scene");
  scene->getObjectManager()->addObject(makeModelObject("Cube", modelUUID, textureUUID));

  EXPECT_EQ(count(textureUUID), 1);
  EXPECT_EQ(count(modelUUID), 1);
}

TEST_F(AssetReferencesTest, CountsAnObjectOnceEvenWhenTwoFieldsMatch)
{
  const auto scene = addScene("11111111-1111-1111-1111-111111111111", "Scene");
  scene->getObjectManager()->addObject(makeModelObject("Cube", modelUUID, modelUUID));
  scene->getObjectManager()->addObject(makeModelObject("Other", unusedUUID, textureUUID));

  EXPECT_EQ(count(modelUUID), 1);
}

TEST_F(AssetReferencesTest, CountsObjectsAcrossEveryScene)
{
  addScene("11111111-1111-1111-1111-111111111111", "First")
    ->getObjectManager()->addObject(makeModelObject("A", modelUUID));
  const auto second = addScene("55555555-5555-5555-5555-555555555555", "Second");
  second->getObjectManager()->addObject(makeModelObject("B", modelUUID));
  second->getObjectManager()->addObject(makeModelObject("C", modelUUID));

  EXPECT_EQ(count(modelUUID), 3);
  EXPECT_EQ(count(unusedUUID), 0);
}

TEST_F(AssetReferencesTest, CountsEveryNodeOfAPrefabBodyReferencingTheAsset)
{
  const auto root = makeModelObject("Root", modelUUID);
  const auto child = makeModelObject("Child", modelUUID);
  const auto bystander = makeModelObject("Bystander", unusedUUID);
  const auto grandchild = makeModelObject("Grandchild", modelUUID);
  child->addChild(grandchild);
  root->addChild(child);
  root->addChild(bystander);

  m_assetRegistry.registerAsset(prefabRecord("Prefab", root->serialize().dump()));

  EXPECT_EQ(count(modelUUID), 3);
  EXPECT_EQ(count(unusedUUID), 1);
}

TEST_F(AssetReferencesTest, SceneObjectsAndPrefabBodiesAddUp)
{
  addScene("11111111-1111-1111-1111-111111111111", "Scene")
    ->getObjectManager()->addObject(makeModelObject("Cube", modelUUID));
  m_assetRegistry.registerAsset(prefabRecord("Prefab", makeModelObject("Body", modelUUID)->serialize().dump()));

  EXPECT_EQ(count(modelUUID), 2);
}

TEST_F(AssetReferencesTest, IgnoresAMalformedPrefabBody)
{
  m_assetRegistry.registerAsset(prefabRecord("Broken", "{ not json " + uuids::to_string(modelUUID)));

  EXPECT_EQ(count(modelUUID), 0);

  m_assetRegistry.registerAsset(prefabRecord("Broken", makeModelObject("Body", modelUUID)->serialize().dump()));

  EXPECT_EQ(count(modelUUID), 1);
}

TEST_F(AssetReferencesTest, IgnoresABodyThatIsNotAnObject)
{
  m_assetRegistry.registerAsset(prefabRecord("List", "[\"" + uuids::to_string(modelUUID) + "\"]"));

  EXPECT_EQ(count(modelUUID), 0);
}

TEST_F(AssetReferencesTest, IgnoresABodyOnARecordThatIsNotAPrefab)
{
  auto record = prefabRecord("Model", makeModelObject("Body", modelUUID)->serialize().dump());
  record.type = AssetType::Model;
  m_assetRegistry.registerAsset(record);

  EXPECT_EQ(count(modelUUID), 0);
}

TEST_F(AssetReferencesTest, CountsAConvexMeshColliderWhoseHullWasBuiltFromTheModel)
{
  const auto scene = addScene("11111111-1111-1111-1111-111111111111", "Scene");

  auto object = std::make_shared<Object>("Hull");
  auto hull = std::make_shared<ConvexMeshCollider>();
  hull->setSourceModel(modelUUID);
  object->addComponent(hull);
  scene->getObjectManager()->addObject(object);

  // The controls: a hull with no source model references nothing, and neither does an unused model uuid.
  auto unrelated = std::make_shared<Object>("Plain");
  unrelated->addComponent(std::make_shared<ConvexMeshCollider>());
  scene->getObjectManager()->addObject(unrelated);

  EXPECT_EQ(count(modelUUID), 1);
  EXPECT_EQ(count(unusedUUID), 0);
}

TEST_F(AssetReferencesTest, CountsAConvexMeshColliderInsideAPrefabBody)
{
  auto object = std::make_shared<Object>("Hull");
  auto hull = std::make_shared<ConvexMeshCollider>();
  hull->setSourceModel(modelUUID);
  object->addComponent(hull);

  m_assetRegistry.registerAsset(prefabRecord("Prefab", object->serialize().dump()));

  EXPECT_EQ(count(modelUUID), 1);
  EXPECT_EQ(count(unusedUUID), 0);
}

TEST_F(AssetReferencesTest, CountsAScriptFieldOnALiveObject)
{
  const auto scene = addScene("11111111-1111-1111-1111-111111111111", "Scene");
  auto object = std::make_shared<Object>("Spawner");
  addScriptWithAssetField(object, modelUUID);
  scene->getObjectManager()->addObject(object);

  EXPECT_EQ(count(modelUUID), 1);
  EXPECT_EQ(count(unusedUUID), 0);
}

TEST_F(AssetReferencesTest, CountsAScriptFieldInsideAPrefabBodyAtTheTopAndInAChild)
{
  const auto root = std::make_shared<Object>("Root");
  addScriptWithAssetField(root, modelUUID);
  const auto child = std::make_shared<Object>("Child");
  addScriptWithAssetField(child, modelUUID);
  const auto bystander = std::make_shared<Object>("Bystander");
  addScriptWithAssetField(bystander, unusedUUID);
  root->addChild(child);
  root->addChild(bystander);

  m_assetRegistry.registerAsset(prefabRecord("Prefab", root->serialize().dump()));

  EXPECT_EQ(count(modelUUID), 2);
  EXPECT_EQ(count(unusedUUID), 1);
}

TEST_F(AssetReferencesTest, ScriptAndComponentReferencesOnOneObjectCountOnce)
{
  const auto scene = addScene("11111111-1111-1111-1111-111111111111", "Scene");
  auto both = makeModelObject("Both", modelUUID);
  addScriptWithAssetField(both, modelUUID);
  scene->getObjectManager()->addObject(both);
  auto scriptOnly = std::make_shared<Object>("ScriptOnly");
  addScriptWithAssetField(scriptOnly, modelUUID);
  scene->getObjectManager()->addObject(scriptOnly);
  scene->getObjectManager()->addObject(makeModelObject("ComponentOnly", modelUUID));

  m_assetRegistry.registerAsset(prefabRecord("Prefab", scriptOnly->serialize().dump()));

  EXPECT_EQ(count(modelUUID), 4);
}

TEST_F(AssetReferencesTest, CountsAReferenceHeldByTheSecondScriptOfAnObject)
{
  const auto scene = addScene("11111111-1111-1111-1111-111111111111", "Scene");
  auto object = std::make_shared<Object>("Two");
  auto first = std::make_shared<Script>("First");
  first->setFields(nlohmann::json::array({
    { { "name", "speed" }, { "type", "float" }, { "value", 1.0 } }
  }));
  object->addComponent(first);
  addScriptWithAssetField(object, modelUUID);
  scene->getObjectManager()->addObject(object);

  EXPECT_EQ(count(modelUUID), 1);
  EXPECT_EQ(count(unusedUUID), 0);
}

TEST_F(AssetReferencesTest, CountsEachSceneScriptHoldingTheAsset)
{
  const auto scene = addScene("11111111-1111-1111-1111-111111111111", "Scene");
  const auto makeSceneScript = [](const std::string& className, const uuids::uuid& asset)
  {
    auto script = std::make_shared<Script>(className);
    script->setFields(nlohmann::json::array({
      { { "name", "model" }, { "type", "string" }, { "asset", "model" }, { "value", uuids::to_string(asset) } }
    }));
    return script;
  };
  EXPECT_TRUE(scene->addScript(makeSceneScript("Alpha", modelUUID)));
  EXPECT_TRUE(scene->addScript(makeSceneScript("Beta", modelUUID)));
  EXPECT_TRUE(scene->addScript(makeSceneScript("Gamma", textureUUID)));

  EXPECT_EQ(count(modelUUID), 2);
  EXPECT_EQ(count(textureUUID), 1);
  EXPECT_EQ(count(unusedUUID), 0);
}
