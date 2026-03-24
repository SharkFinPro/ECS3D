#include "../common/Prefabs.h"
#include "source/ECS3D.h"
#include "source/SaveManager.h"
#include "source/assets/AssetManager.h"
#include "source/scenes/SceneManager.h"
#include "source/assets/SceneAsset.h"
#include <iostream>
#include <random>

constexpr int gridSize = 6;
constexpr int gridHeight = 15;
constexpr int ballSpacing = 5;

void loadScene1(const std::shared_ptr<SceneAsset>& scene,
                const ECS3D& ecs);

void loadScene2(const std::shared_ptr<SceneAsset>& scene,
                const ECS3D& ecs);

void loadScene3(const std::shared_ptr<SceneAsset>& scene,
                const ECS3D& ecs);

int main()
{
  try
  {
    ECS3D ecs;
    ecs.getSaveManager()->loadSaveFile("SetupTest.json");

    const auto m_assetManager = ecs.getAssetManager();
    m_assetManager->loadAsset<ModelAsset>("assets/models/cube_1x1x1.glb");
    m_assetManager->loadAsset<ModelAsset>("assets/models/sphere.glb");
    m_assetManager->loadAsset<ModelAsset>("assets/models/sphere_2.glb");
    m_assetManager->loadAsset<ModelAsset>("assets/models/sphere_3.glb");

    m_assetManager->loadAsset<TextureAsset>("assets/textures/white.png");
    m_assetManager->loadAsset<TextureAsset>("assets/textures/black.png");
    m_assetManager->loadAsset<TextureAsset>("assets/textures/earth.png");
    m_assetManager->loadAsset<TextureAsset>("assets/textures/earth_specular.png");

    m_assetManager->loadScriptAsset("scripts/userScripts/PlayerScript.cs", "PlayerScript");
    m_assetManager->loadScriptAsset("scripts/userScripts/BlockScript.cs", "BlockScript");

    if (const auto sceneManager = ecs.getSceneManager(); !sceneManager->getCurrentScene())
    {
      const auto assetManager = ecs.getAssetManager();
      const auto scene1 = assetManager->createSceneAsset("Scene 1");
      loadScene1(scene1, ecs);
      loadScene2(assetManager->createSceneAsset("Scene 2"), ecs);
      loadScene3(assetManager->createSceneAsset("Scene 3"), ecs);

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

  createRigidBlock({{ 0, -10, 0 }, { 10, 1, 10 }}, assetManager, objectManager);

  createBlock({{ 5, 5, 0}}, assetManager, objectManager);

  createRigidBlock({{ 15, -15, 0 }, {10, 0.25, 10}, {0, 0, 30}}, assetManager, objectManager);

  createSphere({{ 2, 0, 0 }}, assetManager, objectManager);
  createSphere({{ 0, 2, 0 }}, assetManager, objectManager);
  createSphere({{ 0, -2, 2 }}, assetManager, objectManager);

  createPlayer({{ 5, 0, 5 }}, assetManager, objectManager);
}

void loadScene2(const std::shared_ptr<SceneAsset>& scene,
                const ECS3D& ecs)
{
  const auto assetManager = ecs.getAssetManager();
  const auto objectManager = scene->getObjectManager();

  createLight({0, 1.0f, 0}, {1.0f, 1.0f, 1.0f}, 0.25f, 0.0f, 0.0f, objectManager);
  createLight({-10, -0.375f, 3}, {0.0f, 1.0f, 1.0f}, 0.0f, 0.75f, 0.75f, objectManager);
  createLight({10, -0.375f, 3}, {1.0f, 0.0f, 0.0f}, 0.0f, 0.75f, 0.75f, objectManager);

  createRigidBlock({{ 0, -10, 0 }, { 10, 1, 10 }}, assetManager, objectManager);

  createRigidBlock({{ 18, -5, 0 }, { 10, 1, 10 }, { 0, 0, 30}}, assetManager, objectManager);
  createRigidBlock({{ -18, -5, 0 }, { 10, 1, 10 }, { 0, 0, -30}}, assetManager, objectManager);

  createBlock({{ -22, 10, -3 }}, assetManager, objectManager);
  createSphere({{ 2, 0, 3 }}, assetManager, objectManager);
  createPlayer({{ 5, 0, 5 }}, assetManager, objectManager);
}

void loadScene3(const std::shared_ptr<SceneAsset>& scene,
                const ECS3D& ecs)
{
  const auto assetManager = ecs.getAssetManager();
  const auto objectManager = scene->getObjectManager();

  createLight({0, 1.0f, 0}, {1.0f, 1.0f, 1.0f}, 0.25f, 0.0f, 0.0f, objectManager);
  createLight({-25, 25.0f, 3}, {0.0f, 1.0f, 1.0f}, 0.0f, 0.75f, 0.75f, objectManager);
  createLight({25, 25.0f, 3}, {1.0f, 0.0f, 0.0f}, 0.0f, 0.75f, 0.75f, objectManager);

  createRigidBlock({{0, -9, 0}, {100, 10, 100}}, assetManager, objectManager);

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
          createSphere({
            .position = {
              static_cast<float>(j) * ballSpacing + dist(gen) - offsetXZ,
              static_cast<float>(i) * ballSpacing + bottomY,
              static_cast<float>(k) * ballSpacing + dist(gen) - offsetXZ
            },
            .scale = glm::vec3(sphereSize(gen))
          }, assetManager, objectManager);

          continue;
        }

        createBlock({
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
        }, assetManager, objectManager);
      }
    }
  }
}