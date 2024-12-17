#include "source/ECS3D.h"
#include <iostream>
#include <random>

#include "source/objects/Object.h"
#include "source/objects/components/Components.h"

#include "source/scenes/SceneManager.h"
#include "source/scenes/Scene.h"

constexpr int gridSize = 6;
constexpr int gridHeight = 10;
constexpr int ballSpacing = 5;

std::shared_ptr<Object> loadScene1(const std::shared_ptr<Scene>& scene);

void loadScene2(const std::shared_ptr<Scene>& scene);

void loadScene3(const std::shared_ptr<Scene>& scene);

int main()
{
  try
  {
    ECS3D ecs;

    ImGui::SetCurrentContext(VulkanEngine::getImGuiContext());

    SceneManager sceneManager(&ecs);

    const auto scene1 = sceneManager.createScene();
    const auto object = loadScene1(scene1);
    const auto transform = std::dynamic_pointer_cast<Transform>(object->getComponent(ComponentType::transform));
    const auto rigidBody = std::dynamic_pointer_cast<RigidBody>(object->getComponent(ComponentType::rigidBody));

    const auto scene2 = sceneManager.createScene();
    loadScene2(scene2);

    const auto scene3 = sceneManager.createScene();
    loadScene3(scene3);

    int currentScene = 1;
    sceneManager.loadScene(currentScene);

    while (ecs.isActive())
    {
      if (ecs.keyIsPressed(GLFW_KEY_1))
      {
        currentScene = 1;
        sceneManager.loadScene(currentScene);
      }
      else if (ecs.keyIsPressed(GLFW_KEY_2))
      {
        currentScene = 2;
        sceneManager.loadScene(currentScene);
      }
      else if (ecs.keyIsPressed(GLFW_KEY_3))
      {
        currentScene = 3;
        sceneManager.loadScene(currentScene);
      }

      if (currentScene == 1)
      {
        glm::vec3 position = transform->getPosition();
        glm::vec3 rotation = transform->getRotation();
        glm::vec3 scale = transform->getScale();

        ImGui::Begin("Object");

        ImGui::PushID(1);
        ImGui::Text("Control Position:");
        ImGui::SliderFloat("x", &position.x, -30.0f, 30.0f);
        ImGui::SliderFloat("y", &position.y, -30.0f, 30.0f);
        ImGui::SliderFloat("z", &position.z, -30.0f, 30.0f);
        ImGui::PopID();

        ImGui::PushID(2);
        ImGui::Text("Control Rotation:");
        ImGui::SliderFloat("x", &rotation.x, 0.0f, 360.0f);
        ImGui::SliderFloat("y", &rotation.y, 0.0f, 360.0f);
        ImGui::SliderFloat("z", &rotation.z, 0.0f, 360.0f);
        ImGui::PopID();

        ImGui::PushID(3);
        ImGui::Text("Control Scale:");
        ImGui::SliderFloat("x", &scale.x, 0.1f, 10.0f);
        ImGui::SliderFloat("y", &scale.y, 0.1f, 10.0f);
        ImGui::SliderFloat("z", &scale.z, 0.1f, 10.0f);
        ImGui::PopID();

        if (ImGui::Button("Reset"))
        {
          transform->reset();
          rigidBody->setVelocity({ 0, 0, 0 });
        }
        else
        {
          transform->move(position - transform->getPosition());
          transform->setRotation(rotation);
          transform->setScale(scale);
        }
        ImGui::End();
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

std::shared_ptr<Object> loadScene1(const std::shared_ptr<Scene>& scene)
{
  scene->createRigidBlock({{ 0, -10, 0 }, { 10, 1, 10 }});

  std::shared_ptr<Object> obj;
  scene->createBlock({{ 5, 5, 0}}, &obj);

  scene->createRigidBlock({{ 15, -15, 0 }, {10, 0.25, 10}, {0, 0, 30}});

  scene->createSphere({{ 2, 0, 0 }});
  scene->createSphere({{ 0, 2, 0 }});
  scene->createSphere({{ 0, -2, 2 }});

  scene->createPlayer({{ 5, 0, 5 }});

  return obj;
}

void loadScene2(const std::shared_ptr<Scene>& scene)
{
  scene->createRigidBlock({{ 0, -10, 0 }, { 10, 1, 10 }});

  scene->createRigidBlock({{ 18, -5, 0 }, { 10, 1, 10 }, { 0, 0, 30}});
  scene->createRigidBlock({{ -18, -5, 0 }, { 10, 1, 10 }, { 0, 0, -30}});

  scene->createBlock({{ -22, 10, -3 }});
  scene->createSphere({{ 2, 0, 3 }});
  scene->createPlayer({{ 5, 0, 5 }});
}

void loadScene3(const std::shared_ptr<Scene>& scene)
{
  scene->createRigidBlock({{0, -15, 0}, {100, 10, 100}});

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
        if (shouldUseSphere(gen))
        {
          scene->createSphere({
            .position = {
              static_cast<float>(j) * ballSpacing + dist(gen) - (gridSize * ballSpacing / 2.0f),
              static_cast<float>(i) * ballSpacing,
              static_cast<float>(k) * ballSpacing + dist(gen)
            },
            .scale = glm::vec3(sphereSize(gen))
          }, nullptr);

          continue;
        }

        scene->createBlock({
          .position = {
            static_cast<float>(j) * ballSpacing + dist(gen) - (gridSize * ballSpacing / 2.0f),
            static_cast<float>(i) * ballSpacing,
            static_cast<float>(k) * ballSpacing + dist(gen)
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
        }, nullptr);
      }
    }
  }
}