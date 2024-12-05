#include "source/ECS3D.h"
#include <iostream>

#include "source/objects/Object.h"
#include "source/objects/ObjectManager.h"
#include "source/objects/components/Components.h"

std::shared_ptr<Object> createBlock(const ECS3D& ecs, glm::vec3 position = { 0, 0, 0 },
                                    glm::vec3 scale = { 1, 1, 1 }, glm::vec3 rotation = { 0, 0, 0 });
std::shared_ptr<Object> createRigidBlock(const ECS3D& ecs, glm::vec3 position = { 0, 0, 0 },
                                         glm::vec3 scale = { 1, 1, 1 }, glm::vec3 rotation = { 0, 0, 0 });

std::shared_ptr<Object> createSphere(const ECS3D& ecs, glm::vec3 position = { 0, 0, 0 },
                                     glm::vec3 scale = { 1, 1, 1 }, glm::vec3 rotation = { 0, 0, 0 });

int main()
{
  try
  {
    ECS3D ecs;

    const auto object = createRigidBlock(ecs, { 0, -10, 0 }, { 10, 1, 10 });
    ecs.getObjectManager()->addObject(object);

    const auto object2 = createRigidBlock(ecs, { 0, 5, 0});
    ecs.getObjectManager()->addObject(object2);

    ecs.getObjectManager()->addObject(createRigidBlock(ecs, { 15, -15, 0 }, {10, 0.25, 10}, {0, 0, 30}));

    ecs.getObjectManager()->addObject(createSphere(ecs, { 0, 0, 0 }));
    ecs.getObjectManager()->addObject(createSphere(ecs, { 0, 2, 0 }));
    ecs.getObjectManager()->addObject(createSphere(ecs, { 0, -2, 0 }));

    const auto transform = std::dynamic_pointer_cast<Transform>(object2->getComponent(ComponentType::transform));

    while (ecs.isActive())
    {
      glm::vec3 position = transform->getPosition();

      ImGui::Begin("Object");
      ImGui::Text("Control Position:");
      ImGui::SliderFloat("x", &position.x, -30.0f, 30.0f);
      ImGui::SliderFloat("y", &position.y, -30.0f, 30.0f);
      ImGui::SliderFloat("z", &position.z, -30.0f, 30.0f);
      ImGui::End();

      transform->move(position - transform->getPosition());

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

std::shared_ptr<Object> createBlock(const ECS3D& ecs, glm::vec3 position, glm::vec3 scale, glm::vec3 rotation)
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(position, scale, rotation),
    std::make_shared<ModelRenderer>(ecs.getRenderer(),
                                    "assets/textures/white.png",
                                    "assets/textures/blank_specular.png",
                                    "assets/models/cube_1x1x1.glb"),
    std::make_shared<RigidBody>(),
    std::make_shared<BoxCollider>()
  };

  return std::make_shared<Object>(components);
}

std::shared_ptr<Object> createRigidBlock(const ECS3D& ecs, glm::vec3 position, glm::vec3 scale, glm::vec3 rotation)
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(position, scale, rotation),
    std::make_shared<ModelRenderer>(ecs.getRenderer(),
                                    "assets/textures/white.png",
                                    "assets/textures/blank_specular.png",
                                    "assets/models/cube_1x1x1.glb"),
    std::make_shared<BoxCollider>()
  };

  return std::make_shared<Object>(components);
}

std::shared_ptr<Object> createSphere(const ECS3D& ecs, glm::vec3 position, glm::vec3 scale, glm::vec3 rotation)
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(position, scale, rotation),
    std::make_shared<ModelRenderer>(ecs.getRenderer(),
                                    "assets/textures/white.png",
                                    "assets/textures/blank_specular.png",
                                    "assets/models/sphere_3.glb"),
    std::make_shared<RigidBody>(),
    std::make_shared<SphereCollider>()
  };

  return std::make_shared<Object>(components);
}