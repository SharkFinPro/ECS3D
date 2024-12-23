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
  : sceneManager(sceneManager), assetManager(sceneManager->getECS()->getAssetManager()),
    objectManager(std::make_shared<ObjectManager>())
{
  objectManager->setECS(sceneManager->getECS());

  assetManager->loadModel("assets/models/cube_1x1x1.glb");
  assetManager->loadModel("assets/models/sphere.glb");
  assetManager->loadModel("assets/models/sphere_3.glb");

  assetManager->loadTexture("assets/textures/white.png");
  assetManager->loadTexture("assets/textures/black.png");
  assetManager->loadTexture("assets/textures/earth.png");
  assetManager->loadTexture("assets/textures/earth_specular.png");
}

void Scene::load() const
{
  objectManager->resetObjects();
}

void Scene::update(const float dt) const
{
  objectManager->update(dt);
}

void Scene::createBlock(TransformData transformData) const
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(transformData.position, transformData.scale, transformData.rotation),
    std::make_shared<ModelRenderer>(sceneManager->getECS()->getRenderer(),
                                    assetManager->getTexture("assets/textures/white.png")->getTexture(),
                                    assetManager->getTexture("assets/textures/white.png")->getTexture(),
                                    assetManager->getModel("assets/models/cube_1x1x1.glb")->getModel()),
    std::make_shared<RigidBody>(),
    std::make_shared<BoxCollider>()
  };

  objectManager->addObject(std::make_shared<Object>(components, "Block"));
}

void Scene::createRigidBlock(TransformData transformData) const
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(transformData.position, transformData.scale, transformData.rotation),
    std::make_shared<ModelRenderer>(sceneManager->getECS()->getRenderer(),
                                    assetManager->getTexture("assets/textures/white.png")->getTexture(),
                                    assetManager->getTexture("assets/textures/white.png")->getTexture(),
                                    assetManager->getModel("assets/models/cube_1x1x1.glb")->getModel()),
    std::make_shared<BoxCollider>()
  };

  objectManager->addObject(std::make_shared<Object>(components, "Rigid Block"));
}

void Scene::createSphere(TransformData transformData) const
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(transformData.position, transformData.scale, transformData.rotation),
    std::make_shared<ModelRenderer>(sceneManager->getECS()->getRenderer(),
                                    assetManager->getTexture("assets/textures/earth.png")->getTexture(),
                                    assetManager->getTexture("assets/textures/earth_specular.png")->getTexture(),
                                    assetManager->getModel("assets/models/sphere_3.glb")->getModel()),
    std::make_shared<RigidBody>(),
    std::make_shared<SphereCollider>()
  };

  objectManager->addObject(std::make_shared<Object>(components, "Sphere"));
}

void Scene::createPlayer(TransformData transformData) const
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(transformData.position, transformData.scale, transformData.rotation),
    std::make_shared<ModelRenderer>(sceneManager->getECS()->getRenderer(),
                                    assetManager->getTexture("assets/textures/white.png")->getTexture(),
                                    assetManager->getTexture("assets/textures/white.png")->getTexture(),
                                    assetManager->getModel("assets/models/sphere.glb")->getModel()),
    std::make_shared<Player>(),
    std::make_shared<RigidBody>(),
    std::make_shared<SphereCollider>(),
  };

  objectManager->addObject(std::make_shared<Object>(components, "Player"));
}

void Scene::createLight(glm::vec3 position, glm::vec3 color, float ambient, float diffuse, float specular) const
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(position, glm::vec3(1), glm::vec3(0)),
    std::make_shared<LightRenderer>(position, color, ambient, diffuse, specular)
  };

  objectManager->addObject(std::make_shared<Object>(components, "Light"));
}
