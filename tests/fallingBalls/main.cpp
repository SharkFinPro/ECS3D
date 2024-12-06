#include "source/ECS3D.h"
#include <iostream>

#include "source/objects/Object.h"
#include "source/objects/ObjectManager.h"
#include "source/objects/components/Components.h"

#include <random>

constexpr int gridSize = 5;
constexpr int gridHeight = 5;
constexpr int ballSpacing = 3;

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

    ecs.getObjectManager()->addObject(createRigidBlock(ecs, {0, -15, 0}, {100, 3, 100}));

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(-0.5f, 0.5f);

    for (int i = 0; i < gridHeight; i++)
      for (int j = 0; j < gridSize; j++)
        for (int k = 0; k < gridSize; k++)
          ecs.getObjectManager()->addObject(createSphere(ecs,
        {
          j * ballSpacing + dist(gen) - (gridSize * ballSpacing / 2.0f),
          i * ballSpacing,
          k * ballSpacing + dist(gen)
        }));

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