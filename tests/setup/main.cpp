#include "source/ECS3D.h"
#include <iostream>

#include "source/objects/Object.h"
#include "source/objects/components/Components.h"

#include "source/scenes/SceneManager.h"
#include "source/scenes/Scene.h"

void loadScene1(const std::shared_ptr<Scene>& scene);

void loadScene2(const std::shared_ptr<Scene>& scene);

int main()
{
  try
  {
    ECS3D ecs;

    ImGui::SetCurrentContext(VulkanEngine::getImGuiContext());

    SceneManager sceneManager(&ecs);

    const auto scene1 = sceneManager.createScene();
    loadScene1(scene1);

    const auto scene2 = sceneManager.createScene();
    loadScene2(scene2);

    sceneManager.loadScene(1);

    while (ecs.isActive())
    {
      if (ecs.keyIsPressed(GLFW_KEY_1))
      {
        sceneManager.loadScene(1);
      }
      if (ecs.keyIsPressed(GLFW_KEY_2))
      {
        sceneManager.loadScene(2);
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
  scene->createRigidBlock({{ 0, -10, 0 }, { 10, 1, 10 }});

  scene->createRigidBlock({{ 18, -5, 0 }, { 10, 1, 10 }, { 0, 0, 30}});
  scene->createRigidBlock({{ -18, -5, 0 }, { 10, 1, 10 }, { 0, 0, -30}});

  scene->createSphere({{ 2, 0, 0 }});
  scene->createPlayer({{ 5, 0, 5 }});
}
