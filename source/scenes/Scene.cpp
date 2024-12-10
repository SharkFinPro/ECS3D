#include "Scene.h"

#include "SceneManager.h"
#include "../objects/Object.h"
#include "../objects/ObjectManager.h"
#include "../objects/components/Components.h"
#include "../ECS3D.h"

Scene::Scene(SceneManager* sceneManager)
  : sceneManager(sceneManager), objectManager(std::make_shared<ObjectManager>())
{
  objectManager->setECS(sceneManager->getECS());
}

void Scene::load() const
{
  sceneManager->getECS()->setObjectManager(objectManager);
  objectManager->enableRendering();
  objectManager->resetObjects();
}

void Scene::unload() const
{
  objectManager->disableRendering();
}

void Scene::createBlock(TransformData transformData, std::shared_ptr<Object>* object) const
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(transformData.position, transformData.scale, transformData.rotation),
    std::make_shared<ModelRenderer>(sceneManager->getECS()->getRenderer(),
                                    "assets/textures/white.png",
                                    "assets/textures/white.png",
                                    "assets/models/cube_1x1x1.glb"),
    std::make_shared<RigidBody>(),
    std::make_shared<BoxCollider>()
  };

  const auto newObject = std::make_shared<Object>(components);
  objectManager->addObject(newObject);

  if (object)
  {
    *object = newObject;
  }
}

void Scene::createRigidBlock(TransformData transformData, std::shared_ptr<Object>* object) const
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(transformData.position, transformData.scale, transformData.rotation),
    std::make_shared<ModelRenderer>(sceneManager->getECS()->getRenderer(),
                                    "assets/textures/white.png",
                                    "assets/textures/white.png",
                                    "assets/models/cube_1x1x1.glb"),
    std::make_shared<BoxCollider>()
  };

  const auto newObject = std::make_shared<Object>(components);
  objectManager->addObject(newObject);

  if (object)
  {
    *object = newObject;
  }
}

void Scene::createSphere(TransformData transformData, std::shared_ptr<Object>* object) const
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(transformData.position, transformData.scale, transformData.rotation),
    std::make_shared<ModelRenderer>(sceneManager->getECS()->getRenderer(),
                                    "assets/textures/s.png",
                                    "assets/textures/white.png",
                                    "assets/models/sphere_3.glb"),
    std::make_shared<RigidBody>(),
    std::make_shared<SphereCollider>()
  };

  const auto newObject = std::make_shared<Object>(components);
  objectManager->addObject(newObject);

  if (object)
  {
    *object = newObject;
  }
}

void Scene::createPlayer(TransformData transformData, std::shared_ptr<Object>* object) const
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(transformData.position, transformData.scale, transformData.rotation),
    std::make_shared<ModelRenderer>(sceneManager->getECS()->getRenderer(),
                                    "assets/textures/white.png",
                                    "assets/textures/white.png",
                                    "assets/models/sphere.glb"),
    std::make_shared<RigidBody>(),
    std::make_shared<SphereCollider>(),
    std::make_shared<Player>()
  };

  const auto newObject = std::make_shared<Object>(components);
  objectManager->addObject(newObject);

  if (object)
  {
    *object = newObject;
  }
}
