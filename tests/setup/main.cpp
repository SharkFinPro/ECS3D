#include "source/ECS3D.h"
#include <iostream>

#include "source/objects/Object.h"
#include "source/objects/ObjectManager.h"
#include "source/objects/components/Components.h"

std::shared_ptr<Object> createBlock(const ECS3D& ecs, glm::vec3 position = { 0, 0, 0 });
std::shared_ptr<Object> createRigidBlock(const ECS3D& ecs, glm::vec3 position = { 0, 0, 0 });

int main()
{
  try
  {
    ECS3D ecs;

    const auto object = createRigidBlock(ecs);
    ecs.getObjectManager()->addObject(object);

    const auto object2 = createBlock(ecs);
    ecs.getObjectManager()->addObject(object2);

    while (ecs.isActive())
    {
      ecs.update();
    }
  }
  catch (const std::exception& e)
  {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

std::shared_ptr<Object> createBlock(const ECS3D& ecs, glm::vec3 position)
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(position),
    std::make_shared<ModelRenderer>(ecs.getRenderer(),
                                    "assets/textures/white.png",
                                    "assets/textures/blank_specular.png",
                                    "assets/models/cube_1x1x1.glb"),
    std::make_shared<RigidBody>()
  };

  return std::make_shared<Object>(components);
}

std::shared_ptr<Object> createRigidBlock(const ECS3D& ecs, glm::vec3 position)
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(position),
    std::make_shared<ModelRenderer>(ecs.getRenderer(),
                                    "assets/textures/white.png",
                                    "assets/textures/blank_specular.png",
                                    "assets/models/cube_1x1x1.glb")
  };

  return std::make_shared<Object>(components);
}