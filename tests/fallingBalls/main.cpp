#include "../common/Prefabs.h"
#include "source/ECS3D.h"
#include "source/SaveManager.h"
#include "source/assets/AssetManager.h"
#include "source/scenes/SceneManager.h"
#include "source/assets/SceneAsset.h"
#include <iostream>
#include <random>

constexpr int gridSize = 5;
constexpr int gridHeight = 5;
constexpr int ballSpacing = 3;

void loadScene1(const std::shared_ptr<SceneAsset>& scene,
                const ECS3D& ecs);

int main()
{
  try
  {
    ECS3D ecs;
    ecs.getSaveManager()->loadSaveFile("FallingBallsTest.json");

    if (const auto sceneManager = ecs.getSceneManager(); !sceneManager->getCurrentScene())
    {
      const auto assetManager = ecs.getAssetManager();
      const auto scene1 = assetManager->createSceneAsset("Scene 1");
      loadScene1(scene1, ecs);

      sceneManager->loadScene(scene1);
    }

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

void loadScene1(const std::shared_ptr<SceneAsset>& scene,
                const ECS3D& ecs)
{
  const auto assetManager = ecs.getAssetManager();
  const auto objectManager = scene->getObjectManager();

  createLight({0, 1.0f, 0}, {1.0f, 1.0f, 1.0f}, 0.25f, 0.0f, 0.0f, objectManager);
  createLight({-10, -0.375f, 3}, {0.0f, 1.0f, 1.0f}, 0.0f, 0.75f, 0.75f, objectManager);
  createLight({10, -0.375f, 3}, {1.0f, 0.0f, 0.0f}, 0.0f, 0.75f, 0.75f, objectManager);

  createRigidBlock({{0, -15, 0}, {100, 3, 100}}, assetManager, objectManager);

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<float> dist(-0.5f, 0.5f);

  for (int i = 0; i < gridHeight; i++)
  {
    for (int j = 0; j < gridSize; j++)
    {
      for (int k = 0; k < gridSize; k++)
      {
        createSphere({{
          static_cast<float>(j) * ballSpacing + dist(gen) - (gridSize * ballSpacing / 2.0f),
          static_cast<float>(i) * ballSpacing,
          static_cast<float>(k) * ballSpacing + dist(gen)
        }}, assetManager, objectManager);
      }
    }
  }
}