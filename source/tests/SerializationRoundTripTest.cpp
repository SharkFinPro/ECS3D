#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "ProjectPacker.h"
#include "ProjectSerializer.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Camera.h"
#include "objects/components/LightRenderer.h"
#include "objects/components/ModelRenderer.h"
#include "objects/components/PlayerController.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Script.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/SphereCollider.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"

#include <Protocol.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <glm/vec3.hpp>
#include <memory>
#include <string>
#include <vector>

namespace {
  // A whole project's live state, held together so a test can build one and rebuild into another.
  struct Project {
    std::shared_ptr<ComponentRegistry> componentRegistry = std::make_shared<ComponentRegistry>();
    std::unique_ptr<AssetRegistry> assetRegistry = std::make_unique<AssetRegistry>();
    std::unique_ptr<SceneManager> sceneManager = std::make_unique<SceneManager>();
    std::unique_ptr<ProjectSerializer> serializer;
    std::unique_ptr<ProjectPacker> packer;
  };

  Project makeProject()
  {
    Project project;
    registerDataComponents(*project.componentRegistry);

    project.serializer = std::make_unique<ProjectSerializer>(project.assetRegistry.get(),
                                                             project.sceneManager.get(),
                                                             project.componentRegistry);
    project.packer = std::make_unique<ProjectPacker>(project.assetRegistry.get(),
                                                     project.sceneManager.get(),
                                                     project.componentRegistry);

    return project;
  }

  uuids::uuid uuidFrom(const std::string& text)
  {
    return uuids::uuid::from_string(text).value();
  }

  // Assets and an object's components come out of unordered containers, so their serialized order is
  // not stable even between two serializations of the same project. Arrays of scalars are left alone,
  // since a vector's component order is meaningful.
  nlohmann::json canonical(const nlohmann::json& value)
  {
    if (value.is_object())
    {
      nlohmann::json result = nlohmann::json::object();

      for (const auto& [key, item] : value.items())
      {
        result[key] = canonical(item);
      }

      return result;
    }

    if (!value.is_array())
    {
      return value;
    }

    std::vector<nlohmann::json> items;
    items.reserve(value.size());

    for (const auto& item : value)
    {
      items.push_back(canonical(item));
    }

    if (!items.empty() && items.front().is_object())
    {
      std::ranges::sort(items, [](const nlohmann::json& first, const nlohmann::json& second) {
        return first.dump() < second.dump();
      });
    }

    return items;
  }

  std::shared_ptr<Object> addObject(const std::shared_ptr<SceneAsset>& scene, const std::string& name,
                                    const std::shared_ptr<Object>& parent = nullptr)
  {
    auto object = std::make_shared<Object>(name);

    if (parent)
    {
      object->setParent(parent);
    }

    scene->getObjectManager()->addObject(object);

    return object;
  }

  // A project touching every component type, a nested object, a second scene, a prefab body and a
  // display-name override.
  void buildProject(const Project& project)
  {
    const auto modelUUID = uuidFrom("11111111-1111-1111-1111-111111111111");
    const auto textureUUID = uuidFrom("22222222-2222-2222-2222-222222222222");

    project.assetRegistry->registerAsset({ .uuid = modelUUID, .type = AssetType::Model,
                                           .path = "assets/models/cube.glb" });
    project.assetRegistry->registerAsset({ .uuid = textureUUID, .type = AssetType::Texture,
                                           .path = "assets/textures/wood.png" });
    project.assetRegistry->registerAsset({ .uuid = uuidFrom("33333333-3333-3333-3333-333333333333"),
                                           .type = AssetType::Script,
                                           .path = "scripts/UserScripts/Player.cs",
                                           .className = "PlayerScript" });

    const nlohmann::json prefabBody = {
      { "name", "Block" },
      { "uuid", "55555555-5555-5555-5555-555555555555" },
      { "children", nlohmann::json::array() },
      { "components", nlohmann::json::array() },
      { "scripts", nlohmann::json::array() }
    };

    project.assetRegistry->registerAsset({ .uuid = uuidFrom("44444444-4444-4444-4444-444444444444"),
                                           .type = AssetType::Prefab, .path = "Block",
                                           .body = prefabBody.dump() });

    project.assetRegistry->renameAsset(textureUUID, "Nicer Wood");

    const auto scene = std::make_shared<SceneAsset>(uuidFrom("66666666-6666-6666-6666-666666666666"),
                                                    "Main", project.componentRegistry);
    project.sceneManager->addScene(scene);

    const auto body = addObject(scene, "Body");
    const auto transform = body->getComponent<Transform>(ComponentType::transform);
    transform->setPosition({ 1.5f, -2.0f, 3.25f });
    transform->setScale({ 2.0f, 2.0f, 2.0f });
    transform->setRotation({ 0.0f, 90.0f, 0.0f });

    const auto rigidBody = std::make_shared<RigidBody>();
    rigidBody->setMass(12.5f);
    rigidBody->setFriction(0.4f);
    rigidBody->setGravity(9.81f);
    rigidBody->setDoGravity(false);
    body->addComponent(rigidBody);

    const auto boxCollider = std::make_shared<BoxCollider>();
    boxCollider->setScale({ 3.0f, 1.0f, 1.0f });
    boxCollider->setPosition({ 0.0f, 0.5f, 0.0f });
    boxCollider->setIsTrigger(true);
    boxCollider->setLayer(2);
    boxCollider->setMask(0xF0u);
    body->addComponent(boxCollider);

    const auto modelRenderer = std::make_shared<ModelRenderer>();
    modelRenderer->setModelUUID(modelUUID);
    modelRenderer->setTextureUUID(textureUUID);
    modelRenderer->setReflectivity(0.75f);
    modelRenderer->setUseStandardPipeline(false);
    body->addComponent(modelRenderer);

    const auto script = std::make_shared<Script>();
    script->setClassName("PlayerScript");
    script->setFields(nlohmann::json{ { "speed", 4.5 }, { "jump", true } });
    body->addComponent(script);

    const auto child = addObject(scene, "Child", body);

    const auto sphereCollider = std::make_shared<SphereCollider>();
    sphereCollider->setRadius(2.5f);
    child->addComponent(sphereCollider);

    const auto playerController = std::make_shared<PlayerController>();
    playerController->setPlayerSlot(3);
    child->addComponent(playerController);

    const auto camera = std::make_shared<Camera>();
    camera->setDirection({ 0.0f, 0.0f, -1.0f });
    camera->setFov(70.0f);
    camera->setNearPlane(0.05f);
    camera->setFarPlane(500.0f);
    camera->setActive(true);
    child->addComponent(camera);

    const auto lamp = addObject(scene, "Lamp");

    const auto light = std::make_shared<LightRenderer>();
    light->setColor({ 0.25f, 0.5f, 0.75f });
    light->setAmbient(0.1f);
    light->setSpotLight(true);
    light->setConeAngle(25.0f);
    lamp->addComponent(light);

    const auto empty = std::make_shared<SceneAsset>(uuidFrom("77777777-7777-7777-7777-777777777777"),
                                                    "Empty", project.componentRegistry);
    project.sceneManager->addScene(empty);

    project.sceneManager->loadScene(scene);
  }
}

TEST(SerializationRoundTrip, TheJsonPathRebuildsAnIdenticalProject)
{
  const auto original = makeProject();
  buildProject(original);

  const auto blob = original.serializer->serialize();

  const auto rebuilt = makeProject();
  rebuilt.serializer->deserialize(blob);

  EXPECT_EQ(canonical(rebuilt.serializer->serialize()), canonical(blob));
}

TEST(SerializationRoundTrip, TheBinaryPathRebuildsAnIdenticalProject)
{
  const auto original = makeProject();
  buildProject(original);

  net::Message message(net::MessageType::snapshot);
  original.packer->pack(message);

  const auto rebuilt = makeProject();
  rebuilt.packer->unpack(message);

  // Both paths ride the same serialize/loadFromJSON contract, so a binary snapshot has to come back as
  // the project the JSON save file describes. One regression here breaks saved projects and
  // desynchronizes clients at the same time.
  EXPECT_EQ(canonical(rebuilt.serializer->serialize()), canonical(original.serializer->serialize()));
}

TEST(SerializationRoundTrip, TheCurrentSceneSurvivesBothPaths)
{
  const auto original = makeProject();
  buildProject(original);

  const auto expected = original.sceneManager->getCurrentScene()->getUUID();

  const auto fromJson = makeProject();
  fromJson.serializer->deserialize(original.serializer->serialize());

  net::Message message(net::MessageType::snapshot);
  original.packer->pack(message);

  const auto fromBinary = makeProject();
  fromBinary.packer->unpack(message);

  ASSERT_NE(fromJson.sceneManager->getCurrentScene(), nullptr);
  ASSERT_NE(fromBinary.sceneManager->getCurrentScene(), nullptr);
  EXPECT_EQ(fromJson.sceneManager->getCurrentScene()->getUUID(), expected);
  EXPECT_EQ(fromBinary.sceneManager->getCurrentScene()->getUUID(), expected);
}

TEST(SerializationRoundTrip, AMalformedBlobLeavesTheProjectIntact)
{
  const auto project = makeProject();
  buildProject(project);

  const auto before = project.serializer->serialize();

  // An unknown component type throws part way through parsing. The commit only happens once the whole
  // blob has parsed, so the live project has to be untouched.
  // Scenes come back in an unordered map's order, so find the populated one rather than assuming.
  auto broken = before;
  bool injected = false;

  for (auto& scene : broken.at("assets").at("scenes"))
  {
    if (!scene.at("objects").empty())
    {
      scene.at("objects").at(0).at("components").push_back(nlohmann::json{ { "type", "NotAComponent" } });
      injected = true;
      break;
    }
  }

  ASSERT_TRUE(injected);

  EXPECT_ANY_THROW(project.serializer->deserialize(broken));
  EXPECT_EQ(canonical(project.serializer->serialize()), canonical(before));
}
