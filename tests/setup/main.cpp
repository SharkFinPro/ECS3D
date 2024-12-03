#include "source/ECS3D.h"
#include <iostream>

#include "source/objects/Object.h"
#include "source/objects/ObjectManager.h"
#include "source/objects/components/ModelRenderer.h"
#include "source/objects/components/Transform.h"

int main()
{
  try
  {
    ECS3D ecs;

    const std::vector<std::shared_ptr<Component>> components {
      std::make_shared<Transform>(glm::vec3{ 0, 0, 0 }),
      std::make_shared<ModelRenderer>(ecs.getRenderer(),
                                      "assets/textures/white.png",
                                      "assets/textures/blank_specular.png",
                                      "assets/models/square.glb")
    };

    const auto object = std::make_shared<Object>(components);

    ecs.getObjectManager()->addObject(object);

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
