#include "Scene.h"
#include "SceneManager.h"
#include "../ECS3D.h"
#include "../objects/Object.h"
#include "../objects/ObjectManager.h"
#include "../objects/components/Components.h"
#include "../objects/components/LightRenderer.h"

Scene::Scene(SceneManager* sceneManager)
  : sceneManager(sceneManager), objectManager(std::make_shared<ObjectManager>())
{
  objectManager->setECS(sceneManager->getECS());
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
                                    sceneManager->getTexture("assets/textures/white.png"),
                                    sceneManager->getTexture("assets/textures/white.png"),
                                    sceneManager->getModel("assets/models/cube_1x1x1.glb")),
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
                                    sceneManager->getTexture("assets/textures/white.png"),
                                    sceneManager->getTexture("assets/textures/white.png"),
                                    sceneManager->getModel("assets/models/cube_1x1x1.glb")),
    std::make_shared<BoxCollider>()
  };

  objectManager->addObject(std::make_shared<Object>(components, "Rigid Block"));
}

void Scene::createSphere(TransformData transformData) const
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(transformData.position, transformData.scale, transformData.rotation),
    std::make_shared<ModelRenderer>(sceneManager->getECS()->getRenderer(),
                                    sceneManager->getTexture("assets/textures/earth.png"),
                                    sceneManager->getTexture("assets/textures/earth_specular.png"),
                                    sceneManager->getModel("assets/models/sphere_3.glb")),
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
                                    sceneManager->getTexture("assets/textures/white.png"),
                                    sceneManager->getTexture("assets/textures/white.png"),
                                    sceneManager->getModel("assets/models/sphere.glb")),
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
