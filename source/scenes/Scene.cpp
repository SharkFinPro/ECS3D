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

Scene::Scene(SceneManager* sceneManager)
  : m_sceneManager(sceneManager), m_assetManager(sceneManager->getECS()->getAssetManager()),
    m_objectManager(std::make_shared<ObjectManager>())
{
  m_objectManager->setECS(sceneManager->getECS());

  m_assetManager->loadAsset<ModelAsset>("assets/models/cube_1x1x1.glb");
  m_assetManager->loadAsset<ModelAsset>("assets/models/sphere.glb");
  m_assetManager->loadAsset<ModelAsset>("assets/models/sphere_2.glb");
  m_assetManager->loadAsset<ModelAsset>("assets/models/sphere_3.glb");

  m_assetManager->loadAsset<TextureAsset>("assets/textures/white.png");
  m_assetManager->loadAsset<TextureAsset>("assets/textures/black.png");
  m_assetManager->loadAsset<TextureAsset>("assets/textures/earth.png");
  m_assetManager->loadAsset<TextureAsset>("assets/textures/earth_specular.png");
}

void Scene::load() const
{
  m_objectManager->resetScene();
}

void Scene::update(const float dt) const
{
  m_objectManager->update(dt);
}

void Scene::createBlock(TransformData transformData) const
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(transformData.position, transformData.scale, transformData.rotation),
    std::make_shared<ModelRenderer>(m_sceneManager->getECS()->getRenderer(),
                                    m_assetManager->getAsset<TextureAsset>("assets/textures/white.png")->getTexture(),
                                    m_assetManager->getAsset<TextureAsset>("assets/textures/white.png")->getTexture(),
                                    m_assetManager->getAsset<ModelAsset>("assets/models/cube_1x1x1.glb")->getModel()),
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
                                    m_assetManager->getAsset<TextureAsset>("assets/textures/white.png")->getTexture(),
                                    m_assetManager->getAsset<TextureAsset>("assets/textures/white.png")->getTexture(),
                                    m_assetManager->getAsset<ModelAsset>("assets/models/cube_1x1x1.glb")->getModel()),
    std::make_shared<BoxCollider>()
  };

  m_objectManager->addObject(std::make_shared<Object>(components, "Rigid Block"));
}

void Scene::createSphere(TransformData transformData) const
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(transformData.position, transformData.scale, transformData.rotation),
    std::make_shared<ModelRenderer>(m_sceneManager->getECS()->getRenderer(),
                                    m_assetManager->getAsset<TextureAsset>("assets/textures/earth.png")->getTexture(),
                                    m_assetManager->getAsset<TextureAsset>("assets/textures/earth_specular.png")->getTexture(),
                                    m_assetManager->getAsset<ModelAsset>("assets/models/sphere_3.glb")->getModel()),
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
                                    m_assetManager->getAsset<TextureAsset>("assets/textures/white.png")->getTexture(),
                                    m_assetManager->getAsset<TextureAsset>("assets/textures/white.png")->getTexture(),
                                    m_assetManager->getAsset<ModelAsset>("assets/models/sphere.glb")->getModel()),
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
    { "name", "scene" },
    { "objects", serializedObjects["objects"] }
  };

  return data;
}

void Scene::loadFromJSON(const nlohmann::json& sceneData) const
{
  for (const auto& objectData : sceneData["objects"])
  {
    std::vector<std::shared_ptr<Component>> components;

    for (const auto& componentData : objectData["components"])
    {
      if (componentData["type"] == "Collider")
      {
        if (componentData["subType"] == "Box")
        {
          components.push_back(std::make_shared<BoxCollider>());
        }
        else if (componentData["subType"] == "Sphere")
        {
          components.push_back(std::make_shared<SphereCollider>());
        }
      }
      else if (componentData["type"] == "LightRenderer")
      {
        components.push_back(std::make_shared<LightRenderer>(
          m_sceneManager->getECS()->getRenderer(),
          glm::vec3(componentData["color"][0], componentData["color"][1], componentData["color"][2]),
          componentData["ambient"],
          componentData["diffuse"],
          componentData["specular"]
        ));
      }
      else if (componentData["type"] == "ModelRenderer")
      {
        components.push_back(std::make_shared<ModelRenderer>(
          m_sceneManager->getECS()->getRenderer(),
          nullptr,
          nullptr,
          nullptr
        ));

      }
      else if (componentData["type"] == "Player")
      {
        components.push_back(std::make_shared<Player>());
      }
      else if (componentData["type"] == "RigidBody")
      {
        components.push_back(std::make_shared<RigidBody>());
      }
      else if (componentData["type"] == "Transform")
      {
        components.push_back(std::make_shared<Transform>(
          glm::vec3(componentData["position"][0], componentData["position"][1], componentData["position"][2]),
          glm::vec3(componentData["scale"][0], componentData["scale"][1], componentData["scale"][2]),
          glm::vec3(componentData["rotation"][0], componentData["rotation"][1], componentData["rotation"][2])
        ));
      }
    }

    m_objectManager->addObject(std::make_shared<Object>(components, objectData["name"]));
  }
}
