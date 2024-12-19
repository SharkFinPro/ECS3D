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

    const auto scene1 = sceneManager->createScene();
    loadScene1(scene1);

    sceneManager->loadScene(1);

    while (ecs.isActive())
    {
      if (ecs.keyIsPressed(GLFW_KEY_1))
      {
        sceneManager->loadScene(1);
      }
      
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
  scene->createRigidBlock({{0, -15, 0}, {100, 3, 100}});

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<float> dist(-0.5f, 0.5f);

  for (int i = 0; i < gridHeight; i++)
    for (int j = 0; j < gridSize; j++)
      for (int k = 0; k < gridSize; k++)
        scene->createSphere({{
          j * ballSpacing + dist(gen) - (gridSize * ballSpacing / 2.0f),
          i * ballSpacing,
          k * ballSpacing + dist(gen)
        }});
}