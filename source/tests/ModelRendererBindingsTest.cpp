#include <gtest/gtest.h>

#include "ObjectManagerFixtures.h"
#include "TestScene.h"

#include "BindingContext.h"
#include "ModelRendererBindings.h"

#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/components/ModelRenderer.h"

#include <memory>
#include <string>
#include <uuid.h>

namespace {
  using objectManagerFixtures::unknownUUID;

  class ModelRendererBindingsTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
      scene = fixtures::makeScene();
      BindingContext::setObjectManager(scene.objectManager.get());
      BindingContext::setAssetRegistry(&registry);

      modelUUID = scene.objectManager->createUUID();
      textureUUID = scene.objectManager->createUUID();
      registry.registerAsset({ modelUUID, AssetType::Model, "models/a.obj", "", "", "" });
      registry.registerAsset({ textureUUID, AssetType::Texture, "textures/a.png", "", "", "" });

      object = fixtures::addObject(scene, "object");
      renderer = std::make_shared<ModelRenderer>();
      object->addComponent(renderer);
      uuid = uuids::to_string(object->getUUID());
    }

    void TearDown() override
    {
      BindingContext::setObjectManager(nullptr);
      BindingContext::setAssetRegistry(nullptr);
      BindingContext::takeSpawned();
      BindingContext::takeDestroyed();
      BindingContext::takeComponentEdits();
    }

    fixtures::Scene scene;
    AssetRegistry registry;
    ModelRendererBindings bindings = ModelRendererBindingsProvider::getBindings();
    std::shared_ptr<Object> object;
    std::shared_ptr<ModelRenderer> renderer;
    std::string uuid;
    uuids::uuid modelUUID;
    uuids::uuid textureUUID;
  };

  TEST_F(ModelRendererBindingsTest, HasReportsTheComponentOnlyForALiveObjectThatCarriesIt)
  {
    EXPECT_TRUE(bindings.has(uuid.c_str()));

    const auto bare = fixtures::addObject(scene, "bare");
    EXPECT_FALSE(bindings.has(uuids::to_string(bare->getUUID()).c_str()));
    EXPECT_FALSE(bindings.has(uuids::to_string(unknownUUID()).c_str()));
    EXPECT_FALSE(bindings.has("not-a-uuid"));
    EXPECT_FALSE(bindings.has(nullptr));
  }

  TEST_F(ModelRendererBindingsTest, GettersReadTheLiveComponent)
  {
    // Nothing assigned yet: a nil uuid reads as an empty string, not the nil uuid's text.
    EXPECT_EQ(std::string(bindings.getModelUUID(uuid.c_str())), "");
    EXPECT_EQ(std::string(bindings.getTextureUUID(uuid.c_str())), "");

    renderer->setModelUUID(modelUUID);
    renderer->setTextureUUID(textureUUID);
    renderer->setShouldRender(true);

    EXPECT_EQ(std::string(bindings.getModelUUID(uuid.c_str())), uuids::to_string(modelUUID));
    EXPECT_EQ(std::string(bindings.getTextureUUID(uuid.c_str())), uuids::to_string(textureUUID));
    EXPECT_TRUE(bindings.getShouldRender(uuid.c_str()));

    renderer->setShouldRender(false);
    EXPECT_FALSE(bindings.getShouldRender(uuid.c_str()));

    EXPECT_TRUE(BindingContext::takeComponentEdits().empty());
  }

  TEST_F(ModelRendererBindingsTest, SetModelAndTextureAssignRegisteredAssetsAndRecordAnEditEach)
  {
    EXPECT_TRUE(bindings.setModelUUID(uuid.c_str(), uuids::to_string(modelUUID).c_str()));
    EXPECT_EQ(renderer->getModelUUID(), modelUUID);

    const auto modelEdits = BindingContext::takeComponentEdits();
    ASSERT_EQ(modelEdits.size(), 1u);
    EXPECT_EQ(modelEdits[0].first, object->getUUID());
    EXPECT_EQ(modelEdits[0].second, renderer);

    // The drain emptied the buffer, so the texture setter has to record on its own.
    EXPECT_TRUE(bindings.setTextureUUID(uuid.c_str(), uuids::to_string(textureUUID).c_str()));
    EXPECT_EQ(renderer->getTextureUUID(), textureUUID);

    const auto textureEdits = BindingContext::takeComponentEdits();
    ASSERT_EQ(textureEdits.size(), 1u);
    EXPECT_EQ(textureEdits[0].first, object->getUUID());
    EXPECT_EQ(textureEdits[0].second, renderer);
  }

  TEST_F(ModelRendererBindingsTest, SetModelAndTextureRefuseWrongTypedUnknownAndMalformedAssets)
  {
    EXPECT_FALSE(bindings.setModelUUID(uuid.c_str(), uuids::to_string(textureUUID).c_str()));
    EXPECT_FALSE(bindings.setTextureUUID(uuid.c_str(), uuids::to_string(modelUUID).c_str()));
    EXPECT_FALSE(bindings.setModelUUID(uuid.c_str(), uuids::to_string(unknownUUID()).c_str()));
    EXPECT_FALSE(bindings.setTextureUUID(uuid.c_str(), uuids::to_string(unknownUUID()).c_str()));
    EXPECT_FALSE(bindings.setModelUUID(uuid.c_str(), "not-a-uuid"));
    EXPECT_FALSE(bindings.setTextureUUID(uuid.c_str(), "not-a-uuid"));
    EXPECT_FALSE(bindings.setModelUUID(uuid.c_str(), nullptr));
    EXPECT_FALSE(bindings.setTextureUUID(uuid.c_str(), nullptr));

    EXPECT_TRUE(renderer->getModelUUID().is_nil());
    EXPECT_TRUE(renderer->getTextureUUID().is_nil());
    EXPECT_TRUE(BindingContext::takeComponentEdits().empty());

    // Positive control: the right type on the same object is accepted.
    EXPECT_TRUE(bindings.setModelUUID(uuid.c_str(), uuids::to_string(modelUUID).c_str()));
    EXPECT_EQ(renderer->getModelUUID(), modelUUID);
  }

  TEST_F(ModelRendererBindingsTest, SettingAnAssetWithNoRegistryInjectedIsRefused)
  {
    BindingContext::setAssetRegistry(nullptr);
    EXPECT_FALSE(bindings.setModelUUID(uuid.c_str(), uuids::to_string(modelUUID).c_str()));
    EXPECT_TRUE(renderer->getModelUUID().is_nil());

    BindingContext::setAssetRegistry(&registry);
    EXPECT_TRUE(bindings.setModelUUID(uuid.c_str(), uuids::to_string(modelUUID).c_str()));
  }

  TEST_F(ModelRendererBindingsTest, SetShouldRenderWritesTheFlagAndRecordsAnEdit)
  {
    ASSERT_FALSE(renderer->getShouldRender());

    bindings.setShouldRender(uuid.c_str(), true);
    EXPECT_TRUE(renderer->getShouldRender());

    const auto edits = BindingContext::takeComponentEdits();
    ASSERT_EQ(edits.size(), 1u);
    EXPECT_EQ(edits[0].first, object->getUUID());
    EXPECT_EQ(edits[0].second, renderer);
  }

  TEST_F(ModelRendererBindingsTest, UnknownObjectReadsNeutralDefaultsAndRecordsNothing)
  {
    const auto bare = fixtures::addObject(scene, "bare");
    const std::string ids[] = { uuids::to_string(unknownUUID()), uuids::to_string(bare->getUUID()),
                                "not-a-uuid" };

    for (const auto& id : ids)
    {
      EXPECT_EQ(std::string(bindings.getModelUUID(id.c_str())), "");
      EXPECT_EQ(std::string(bindings.getTextureUUID(id.c_str())), "");
      EXPECT_FALSE(bindings.getShouldRender(id.c_str()));
      EXPECT_FALSE(bindings.setModelUUID(id.c_str(), uuids::to_string(modelUUID).c_str()));
      EXPECT_FALSE(bindings.setTextureUUID(id.c_str(), uuids::to_string(textureUUID).c_str()));
      bindings.setShouldRender(id.c_str(), true);
    }

    EXPECT_TRUE(BindingContext::takeComponentEdits().empty());
    EXPECT_FALSE(renderer->getShouldRender());
    EXPECT_TRUE(renderer->getModelUUID().is_nil());

    // Positive control: the same call on the live object does record.
    bindings.setShouldRender(uuid.c_str(), true);
    EXPECT_EQ(BindingContext::takeComponentEdits().size(), 1u);
  }
}
