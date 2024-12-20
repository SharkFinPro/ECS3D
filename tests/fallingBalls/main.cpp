#include "source/ECS3D.h"
#include "source/scenes/SceneManager.h"
#include "source/scenes/Scene.h"
#include <iostream>
#include <random>

constexpr int gridSize = 5;
constexpr int gridHeight = 5;
constexpr int ballSpacing = 3;

void loadScene1(const std::shared_ptr<Scene>& scene);

int main()
{
  try
  {
    ECS3D ecs;
    const auto sceneManager = ecs.getSceneManager();

    loadScene1(sceneManager->createScene());

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

  scene->createRigidBlock({{0, -15, 0}, {100, 3, 100}});

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<float> dist(-0.5f, 0.5f);

  for (int i = 0; i < gridHeight; i++)
  {
    for (int j = 0; j < gridSize; j++)
    {
      for (int k = 0; k < gridSize; k++)
      {
        scene->createSphere({{
          static_cast<float>(j) * ballSpacing + dist(gen) - (gridSize * ballSpacing / 2.0f),
          static_cast<float>(i) * ballSpacing,
          static_cast<float>(k) * ballSpacing + dist(gen)
        }});
      }
    }
  }
}