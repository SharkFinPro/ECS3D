#include "Scene.h"
#include "SceneManager.h"
#include "../assets/AssetManager.h"
#include "../assets/TextureAsset.h"
#include "../assets/ModelAsset.h"
#include "../ECS3D.h"
#include "../objects/Object.h"
#include "../objects/ObjectManager.h"
#include "../objects/components/Components.h"
#include "../objects/components/LightRenderer.h"
#include <nlohmann/json.hpp>

Scene::Scene(SceneManager* sceneManager,
             std::string name)
  : m_sceneManager(sceneManager), m_assetManager(sceneManager->getECS()->getAssetManager()),
    m_objectManager(std::make_shared<ObjectManager>(sceneManager->getECS())),
    m_uuid(m_sceneManager->getECS()->createUUID()), m_name(std::move(name))
{
  m_assetManager->loadAsset<ModelAsset>("assets/models/cube_1x1x1.glb");
  m_assetManager->loadAsset<ModelAsset>("assets/models/sphere.glb");
  m_assetManager->loadAsset<ModelAsset>("assets/models/sphere_2.glb");
  m_assetManager->loadAsset<ModelAsset>("assets/models/sphere_3.glb");

  m_assetManager->loadAsset<TextureAsset>("assets/textures/white.png");
  m_assetManager->loadAsset<TextureAsset>("assets/textures/black.png");
  m_assetManager->loadAsset<TextureAsset>("assets/textures/earth.png");
  m_assetManager->loadAsset<TextureAsset>("assets/textures/earth_specular.png");
}

void Scene::update(const float dt) const
{
  m_objectManager->update(dt, m_sceneManager->getSceneStatus() == SceneStatus::running);
}

void Scene::createBlock(TransformData transformData) const
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(transformData.position, transformData.scale, transformData.rotation),
    std::make_shared<ModelRenderer>(m_sceneManager->getECS()->getRenderer(),
                                    m_assetManager->getAsset<TextureAsset>("assets/textures/white.png"),
                                    m_assetManager->getAsset<TextureAsset>("assets/textures/white.png"),
                                    m_assetManager->getAsset<ModelAsset>("assets/models/cube_1x1x1.glb")),
    std::make_shared<RigidBody>(),
    std::make_shared<BoxCollider>()
  };

  m_objectManager->addObject(std::make_shared<Object>(components, "Block"));
}

void Scene::createRigidBlock(TransformData transformData) const
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(transformData.position, transformData.scale, transformData.rotation),
    std::make_shared<ModelRenderer>(m_sceneManager->getECS()->getRenderer(),
                                    m_assetManager->getAsset<TextureAsset>("assets/textures/white.png"),
                                    m_assetManager->getAsset<TextureAsset>("assets/textures/white.png"),
                                    m_assetManager->getAsset<ModelAsset>("assets/models/cube_1x1x1.glb")),
    std::make_shared<BoxCollider>()
  };

  m_objectManager->addObject(std::make_shared<Object>(components, "Rigid Block"));
}

void Scene::createSphere(TransformData transformData) const
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(transformData.position, transformData.scale, transformData.rotation),
    std::make_shared<ModelRenderer>(m_sceneManager->getECS()->getRenderer(),
                                    m_assetManager->getAsset<TextureAsset>("assets/textures/earth.png"),
                                    m_assetManager->getAsset<TextureAsset>("assets/textures/earth_specular.png"),
                                    m_assetManager->getAsset<ModelAsset>("assets/models/sphere_3.glb")),
    std::make_shared<RigidBody>(),
    std::make_shared<SphereCollider>()
  };

  m_objectManager->addObject(std::make_shared<Object>(components, "Sphere"));
}

void Scene::createPlayer(TransformData transformData) const
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(transformData.position, transformData.scale, transformData.rotation),
    std::make_shared<ModelRenderer>(m_sceneManager->getECS()->getRenderer(),
                                    m_assetManager->getAsset<TextureAsset>("assets/textures/white.png"),
                                    m_assetManager->getAsset<TextureAsset>("assets/textures/white.png"),
                                    m_assetManager->getAsset<ModelAsset>("assets/models/sphere.glb")),
    std::make_shared<RigidBody>(),
    std::make_shared<SphereCollider>(),
    std::make_shared<Player>()
  };

  m_objectManager->addObject(std::make_shared<Object>(components, "Player"));
}

void Scene::createLight(glm::vec3 position, glm::vec3 color, float ambient, float diffuse, float specular) const
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(position, glm::vec3(1), glm::vec3(0)),
    std::make_shared<LightRenderer>(m_sceneManager->getECS()->getRenderer(), color, ambient, diffuse, specular)
  };

  m_objectManager->addObject(std::make_shared<Object>(components, "Light"));
}

nlohmann::json Scene::serialize() const
{
  const auto serializedObjects = m_objectManager->serialize();
  nlohmann::json data = {
    { "name", m_name },
    { "objects", serializedObjects["objects"] },
    { "uuid", uuids::to_string(m_uuid) }
  };

  return data;
}

void Scene::loadFromJSON(const nlohmann::json& sceneData)
{
  m_uuid = uuids::uuid::from_string(std::string(sceneData.at("uuid"))).value();
  m_name = sceneData.at("name");

  for (const auto& objectData : sceneData["objects"])
  {
    m_objectManager->addObject(std::make_shared<Object>(objectData, m_objectManager.get()));
  }
}

std::string Scene::getName() const
{
  return m_name;
}

void Scene::start() const
{
  m_objectManager->start();
}

void Scene::stop() const
{
  m_objectManager->stop();
}
