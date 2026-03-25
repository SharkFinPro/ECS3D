#ifndef ECS3D_PREFABS_H
#define ECS3D_PREFABS_H

#include "source/assets/AssetManager.h"
#include "source/assets/TextureAsset.h"
#include "source/assets/ModelAsset.h"
#include "source/assets/ScriptAsset.h"
#include "source/objects/Object.h"
#include "source/objects/ObjectManager.h"
#include "source/objects/components/LightRenderer.h"
#include "source/objects/components/ModelRenderer.h"
#include "source/objects/components/RigidBody.h"
#include "source/objects/components/Script.h"
#include "source/objects/components/Transform.h"
#include "source/objects/components/collisions/BoxCollider.h"
#include "source/objects/components/collisions/SphereCollider.h"
#include <memory>

struct TransformData {
  glm::vec3 position = { 0, 0, 0 };
  glm::vec3 scale = { 1, 1, 1 };
  glm::vec3 rotation = { 0, 0, 0 };
};

inline void createBlock(TransformData transformData,
                        const std::shared_ptr<AssetManager>& assetManager,
                        const std::shared_ptr<ObjectManager>& objectManager)
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(transformData.position, transformData.scale, transformData.rotation),
    std::make_shared<ModelRenderer>(objectManager->getECS()->getRenderer(),
                                    assetManager->getAsset<TextureAsset>("assets/textures/white.png"),
                                    assetManager->getAsset<TextureAsset>("assets/textures/white.png"),
                                    assetManager->getAsset<ModelAsset>("assets/models/cube_1x1x1.glb")),
    std::make_shared<RigidBody>(),
    std::make_shared<BoxCollider>()
  };

  objectManager->addObject(std::make_shared<Object>(components, "Block"));
}

inline void createRigidBlock(TransformData transformData,
                             const std::shared_ptr<AssetManager>& assetManager,
                             const std::shared_ptr<ObjectManager>& objectManager)
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(transformData.position, transformData.scale, transformData.rotation),
    std::make_shared<ModelRenderer>(objectManager->getECS()->getRenderer(),
                                    assetManager->getAsset<TextureAsset>("assets/textures/white.png"),
                                    assetManager->getAsset<TextureAsset>("assets/textures/white.png"),
                                    assetManager->getAsset<ModelAsset>("assets/models/cube_1x1x1.glb")),
    std::make_shared<BoxCollider>()
  };

  objectManager->addObject(std::make_shared<Object>(components, "Rigid Block"));
}

inline void createSphere(TransformData transformData,
                         const std::shared_ptr<AssetManager>& assetManager,
                         const std::shared_ptr<ObjectManager>& objectManager)
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(transformData.position, transformData.scale, transformData.rotation),
    std::make_shared<ModelRenderer>(objectManager->getECS()->getRenderer(),
                                    assetManager->getAsset<TextureAsset>("assets/textures/earth.png"),
                                    assetManager->getAsset<TextureAsset>("assets/textures/earth_specular.png"),
                                    assetManager->getAsset<ModelAsset>("assets/models/sphere_3.glb")),
    std::make_shared<RigidBody>(),
    std::make_shared<SphereCollider>()
  };

  objectManager->addObject(std::make_shared<Object>(components, "Sphere"));
}

inline void createPlayer(TransformData transformData,
                         const std::shared_ptr<AssetManager>& assetManager,
                         const std::shared_ptr<ObjectManager>& objectManager)
{
  const auto playerScript = assetManager->getAsset<ScriptAsset>("scripts/userScripts/PlayerScript.cs");
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(transformData.position, transformData.scale, transformData.rotation),
    std::make_shared<ModelRenderer>(objectManager->getECS()->getRenderer(),
                                    assetManager->getAsset<TextureAsset>("assets/textures/white.png"),
                                    assetManager->getAsset<TextureAsset>("assets/textures/white.png"),
                                    assetManager->getAsset<ModelAsset>("assets/models/sphere.glb")),
    std::make_shared<RigidBody>(),
    std::make_shared<SphereCollider>(),
    playerScript->createScript()
  };

  objectManager->addObject(std::make_shared<Object>(components, "Player"));
}

inline void createLight(glm::vec3 position,
                        glm::vec3 color,
                        float ambient,
                        float diffuse,
                        float specular,
                        const std::shared_ptr<ObjectManager>& objectManager)
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(position, glm::vec3(1), glm::vec3(0)),
    std::make_shared<LightRenderer>(objectManager->getECS()->getRenderer(), color, ambient, diffuse, specular)
  };

  objectManager->addObject(std::make_shared<Object>(components, "Light"));
}

#endif //ECS3D_PREFABS_H