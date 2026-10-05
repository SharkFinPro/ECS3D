#ifndef WIRETRUNCATIONFIXTURES_H
#define WIRETRUNCATIONFIXTURES_H

#include "TestScene.h"
#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "ProjectPacker.h"
#include "ProjectSerializer.h"
#include "Replication.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/LightRenderer.h"
#include "objects/components/ModelRenderer.h"
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
#include <memory>
#include <string>
#include <vector>

// Scenes, projects and payload sources for the truncation sweep.
namespace wiretest {
  using fixtures::Scene;
  using fixtures::makeScene;


  inline bool comesFromAnUnorderedContainer(const std::string& key, const nlohmann::json& array)
  {
    if (key == "models" || key == "textures" || key == "prefabs" || key == "scenes" || key == "components")
    {
      return true;
    }

    return key == "scripts" && !array.empty() && array.front().is_object() && array.front().contains("uuid");
  }

  // Components, assets and scenes serialize out of unordered containers, so equal states can dump in
  // different orders; sort those arrays and leave the vector-backed ones as they are.
  inline nlohmann::json canonical(const nlohmann::json& value, const std::string& key = "")
  {
    if (value.is_object())
    {
      nlohmann::json result = nlohmann::json::object();

      for (const auto& [childKey, item] : value.items())
      {
        result[childKey] = canonical(item, childKey);
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
      items.push_back(canonical(item, key));
    }

    if (comesFromAnUnorderedContainer(key, value))
    {
      std::ranges::sort(items, [](const nlohmann::json& first, const nlohmann::json& second) {
        return first.dump() < second.dump();
      });
    }

    return items;
  }

  inline nlohmann::json stateOf(const Scene& scene)
  {
    return canonical(scene.objectManager->serialize());
  }

  // A parent with a box collider, model renderer and script, a child with a sphere collider, and a
  // grandchild: several strings, nested objects and component kinds in one payload.
  inline std::shared_ptr<Object> buildTree(const Scene& scene)
  {
    const auto parent = addObject(scene, "Spawned Parent", { 1.0f, 2.0f, 3.0f });
    fixtures::addBoxCollider(parent);

    const auto model = std::make_shared<ModelRenderer>();
    model->setModelUUID(uuids::uuid::from_string("11111111-1111-1111-1111-111111111111").value());
    parent->addComponent(model);

    const auto script = std::make_shared<Script>();
    script->setClassName("SweepScript");
    script->setFields(nlohmann::json{ { "speed", 4.5 }, { "label", "hello" } });
    parent->addComponent(script);

    const auto child = addChildObject(scene, "Child A", parent);
    fixtures::addSphereCollider(child, 2.5f);
    addChildObject(scene, "Grandchild", child);

    return parent;
  }

  // The receiving scene already holds something, so "unchanged" is a comparison against real content.
  inline Scene makeResidentScene()
  {
    auto scene = makeScene();
    addObject(scene, "Resident", { 9.0f, 8.0f, 7.0f });

    return scene;
  }

  // A scene holding the same objects, uuids included, as the one packed.
  inline Scene copyOf(const Scene& source)
  {
    auto copy = makeScene();

    net::Message snapshot(net::MessageType::snapshot);
    source.objectManager->pack(snapshot);
    net::MessageReader reader(snapshot);
    copy.objectManager->unpack(reader);

    return copy;
  }

  struct Project {
    std::shared_ptr<ComponentRegistry> componentRegistry = std::make_shared<ComponentRegistry>();
    std::unique_ptr<AssetRegistry> assetRegistry = std::make_unique<AssetRegistry>();
    std::unique_ptr<SceneManager> sceneManager = std::make_unique<SceneManager>();
    std::unique_ptr<ProjectSerializer> serializer;
    std::unique_ptr<ProjectPacker> packer;
  };

  inline Project makeProject()
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

  inline uuids::uuid uuidFrom(const std::string& text)
  {
    return uuids::uuid::from_string(text).value();
  }

  inline std::shared_ptr<SceneAsset> addScene(const Project& project, const std::string& uuid, const std::string& name)
  {
    const auto scene = std::make_shared<SceneAsset>(uuidFrom(uuid), name, project.componentRegistry);
    project.sceneManager->addScene(scene);

    return scene;
  }

  inline void buildSnapshotProject(const Project& project)
  {
    project.assetRegistry->registerAsset({ .uuid = uuidFrom("11111111-1111-1111-1111-111111111111"),
                                           .type = AssetType::Model, .path = "assets/models/cube.glb" });
    project.assetRegistry->registerAsset({ .uuid = uuidFrom("22222222-2222-2222-2222-222222222222"),
                                           .type = AssetType::Script, .path = "scripts/UserScripts/Player.cs",
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
                                           .body = prefabBody.dump(), .displayName = "Fancy Block" });

    const auto main = addScene(project, "66666666-6666-6666-6666-666666666666", "Main");
    const auto body = std::make_shared<Object>("Body");
    main->getObjectManager()->addObject(body);
    body->getComponent<Transform>(ComponentType::transform)->setPosition({ 1.5f, -2.0f, 3.25f });
    body->addComponent(std::make_shared<RigidBody>());
    body->addComponent(std::make_shared<BoxCollider>());

    const auto script = std::make_shared<Script>();
    script->setClassName("PlayerScript");
    script->setFields(nlohmann::json{ { "speed", 4.5 } });
    body->addComponent(script);

    const auto child = std::make_shared<Object>("Child");
    child->setParent(body);
    main->getObjectManager()->addObject(child);
    const auto sphere = std::make_shared<SphereCollider>();
    child->addComponent(sphere);
    sphere->setRadius(2.5f);

    const auto lamp = std::make_shared<Object>("Lamp");
    main->getObjectManager()->addObject(lamp);
    lamp->addComponent(std::make_shared<LightRenderer>());

    addScene(project, "77777777-7777-7777-7777-777777777777", "Empty");
    project.sceneManager->loadScene(main);
  }

  inline void buildResidentProject(const Project& project)
  {
    project.assetRegistry->registerAsset({ .uuid = uuidFrom("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"),
                                           .type = AssetType::Texture, .path = "assets/textures/old.png" });

    const auto scene = addScene(project, "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb", "Resident Scene");
    scene->getObjectManager()->addObject(std::make_shared<Object>("Resident"));
    project.sceneManager->loadScene(scene);
  }

  inline net::Message packInput(const std::vector<int>& keys)
  {
    return replication::buildInputState(true, keys, 10.0f, 20.0f, 1.0f, -1.0f, 0.5f, 5);
  }
}

#endif //WIRETRUNCATIONFIXTURES_H
