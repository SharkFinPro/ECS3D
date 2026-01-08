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
    std::make_shared<LightRenderer>(m_sceneManager->getECS()->getRenderer(), position, color, ambient, diffuse, specular)
  };

  m_objectManager->addObject(std::make_shared<Object>(components, "Light"));
}
