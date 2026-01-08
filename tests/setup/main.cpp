#include "source/ECS3D.h"
#include <iostream>
#include <random>

#include "source/scenes/SceneManager.h"
#include "source/scenes/Scene.h"

constexpr int gridSize = 6;
constexpr int gridHeight = 15;
constexpr int ballSpacing = 5;

void loadScene1(const std::shared_ptr<Scene>& scene);

void loadScene2(const std::shared_ptr<Scene>& scene);

void loadScene3(const std::shared_ptr<Scene>& scene);

int main()
{
  try
  {
    ECS3D ecs;
    const auto sceneManager = ecs.getSceneManager();

    loadScene1(sceneManager->createScene());
    loadScene2(sceneManager->createScene());
    loadScene3(sceneManager->createScene());

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

void loadScene1(const std::shared_ptr<Scene>& scene)
{
  scene->createLight({0, 1.0f, 0}, {1.0f, 1.0f, 1.0f}, 0.25f, 0.0f, 0.0f);
  scene->createLight({-10, -0.375f, 3}, {0.0f, 1.0f, 1.0f}, 0.0f, 0.75f, 0.75f);
  scene->createLight({10, -0.375f, 3}, {1.0f, 0.0f, 0.0f}, 0.0f, 0.75f, 0.75f);

  scene->createRigidBlock({{ 0, -10, 0 }, { 10, 1, 10 }});

  scene->createBlock({{ 5, 5, 0}});

  scene->createRigidBlock({{ 15, -15, 0 }, {10, 0.25, 10}, {0, 0, 30}});

  scene->createSphere({{ 2, 0, 0 }});
  scene->createSphere({{ 0, 2, 0 }});
  scene->createSphere({{ 0, -2, 2 }});

  scene->createPlayer({{ 5, 0, 5 }});
}

void loadScene2(const std::shared_ptr<Scene>& scene)
{
  scene->createLight({0, 1.0f, 0}, {1.0f, 1.0f, 1.0f}, 0.25f, 0.0f, 0.0f);
  scene->createLight({-10, -0.375f, 3}, {0.0f, 1.0f, 1.0f}, 0.0f, 0.75f, 0.75f);
  scene->createLight({10, -0.375f, 3}, {1.0f, 0.0f, 0.0f}, 0.0f, 0.75f, 0.75f);

  scene->createRigidBlock({{ 0, -10, 0 }, { 10, 1, 10 }});

  scene->createRigidBlock({{ 18, -5, 0 }, { 10, 1, 10 }, { 0, 0, 30}});
  scene->createRigidBlock({{ -18, -5, 0 }, { 10, 1, 10 }, { 0, 0, -30}});

  scene->createBlock({{ -22, 10, -3 }});
  scene->createSphere({{ 2, 0, 3 }});
  scene->createPlayer({{ 5, 0, 5 }});
}

void loadScene3(const std::shared_ptr<Scene>& scene)
{
  scene->createLight({0, 1.0f, 0}, {1.0f, 1.0f, 1.0f}, 0.25f, 0.0f, 0.0f);
  scene->createLight({-25, 25.0f, 3}, {0.0f, 1.0f, 1.0f}, 0.0f, 0.75f, 0.75f);
  scene->createLight({25, 25.0f, 3}, {1.0f, 0.0f, 0.0f}, 0.0f, 0.75f, 0.75f);

  scene->createRigidBlock({{0, -9, 0}, {100, 10, 100}});

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<float> dist(-0.75f, 0.75f);
  std::uniform_real_distribution<float> sphereSize(0.25f, 1.5f);
  std::uniform_int_distribution<int> shouldUseSphere(0, 1);

  std::uniform_real_distribution<float> rot(0.0f, 360.0f);

  for (int i = 0; i < gridHeight; i++)
  {
    for (int j = 0; j < gridSize; j++)
    {
      for (int k = 0; k < gridSize; k++)
      {
        constexpr float bottomY = 6.0f;
        constexpr float offsetXZ = (gridSize - 1) * ballSpacing / 2.0f;

        if (shouldUseSphere(gen))
        {
          scene->createSphere({
            .position = {
              static_cast<float>(j) * ballSpacing + dist(gen) - offsetXZ,
              static_cast<float>(i) * ballSpacing + bottomY,
              static_cast<float>(k) * ballSpacing + dist(gen) - offsetXZ
            },
            .scale = glm::vec3(sphereSize(gen))
          });

          continue;
        }

        scene->createBlock({
          .position = {
            static_cast<float>(j) * ballSpacing + dist(gen) - offsetXZ,
            static_cast<float>(i) * ballSpacing + bottomY,
            static_cast<float>(k) * ballSpacing + dist(gen) - offsetXZ
          },
          .scale = {
            sphereSize(gen),
            sphereSize(gen),
            sphereSize(gen)
          },
          .rotation = {
            rot(gen),
            rot(gen),
            rot(gen)
          }
        });
      }
    }
  }
}