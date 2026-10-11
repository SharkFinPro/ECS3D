#include <gtest/gtest.h>

#include <AssetReferences.h>
#include <ComponentRegistration.h>
#include <ComponentRegistry.h>
#include <assets/AssetRegistry.h>
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/ModelRenderer.h>
#include <scenes/SceneAsset.h>
#include <scenes/SceneManager.h>

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
