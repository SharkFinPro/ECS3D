#include "Scene.h"

#include "SceneManager.h"
#include "../objects/Object.h"
#include "../objects/ObjectManager.h"
#include "../objects/components/Components.h"
#include "../ECS3D.h"
#include "glm/gtc/type_ptr.hpp"

Scene::Scene(SceneManager* sceneManager)
  : sceneManager(sceneManager), objectManager(std::make_shared<ObjectManager>())
{
  objectManager->setECS(sceneManager->getECS());

  const auto renderer = sceneManager->getECS()->getRenderer();

  lights.push_back(renderer->createLight({0, 1.0f, 0}, {1.0f, 1.0f, 1.0f}, 0.25f, 0.0f, 0.0f));

  lights.push_back(renderer->createLight({-10, -0.375f, 3}, {0.0f, 1.0f, 1.0f}, 0.0f, 0.75f, 0.75f));

  lights.push_back(renderer->createLight({10, -0.375f, 3}, {1.0f, 0.0f, 0.0f}, 0.0f, 0.75f, 0.75f));
}

void Scene::load() const
{
  objectManager->resetObjects();
}

void Scene::update(const float dt) const
{
  for (int i = 0; i < lights.size(); i++)
  {
    ImGui::Begin("Lights");
    displayLightGui(lights[i], i);
    ImGui::End();

    sceneManager->getECS()->getRenderer()->renderLight(lights[i]);
  }

  objectManager->update(dt);
}

void Scene::createBlock(TransformData transformData) const
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(transformData.position, transformData.scale, transformData.rotation),
    std::make_shared<ModelRenderer>(sceneManager->getECS()->getRenderer(),
                                    sceneManager->getTexture("assets/textures/white.png"),
                                    sceneManager->getTexture("assets/textures/white.png"),
                                    sceneManager->getModel("assets/models/cube_1x1x1.glb")),
    std::make_shared<RigidBody>(),
    std::make_shared<BoxCollider>()
  };

  const auto newObject = std::make_shared<Object>(components);
  objectManager->addObject(newObject);
}

void Scene::createRigidBlock(TransformData transformData) const
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(transformData.position, transformData.scale, transformData.rotation),
    std::make_shared<ModelRenderer>(sceneManager->getECS()->getRenderer(),
                                    sceneManager->getTexture("assets/textures/white.png"),
                                    sceneManager->getTexture("assets/textures/white.png"),
                                    sceneManager->getModel("assets/models/cube_1x1x1.glb")),
    std::make_shared<BoxCollider>()
  };

  const auto newObject = std::make_shared<Object>(components);
  objectManager->addObject(newObject);
}

void Scene::createSphere(TransformData transformData) const
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(transformData.position, transformData.scale, transformData.rotation),
    std::make_shared<ModelRenderer>(sceneManager->getECS()->getRenderer(),
                                    sceneManager->getTexture("assets/textures/earth.png"),
                                    sceneManager->getTexture("assets/textures/earth_specular.png"),
                                    sceneManager->getModel("assets/models/sphere_3.glb")),
    std::make_shared<RigidBody>(),
    std::make_shared<SphereCollider>()
  };

  const auto newObject = std::make_shared<Object>(components);
  objectManager->addObject(newObject);
}

void Scene::createPlayer(TransformData transformData) const
{
  const std::vector<std::shared_ptr<Component>> components {
    std::make_shared<Transform>(transformData.position, transformData.scale, transformData.rotation),
    std::make_shared<ModelRenderer>(sceneManager->getECS()->getRenderer(),
                                    sceneManager->getTexture("assets/textures/white.png"),
                                    sceneManager->getTexture("assets/textures/white.png"),
                                    sceneManager->getModel("assets/models/sphere.glb")),
    std::make_shared<RigidBody>(),
    std::make_shared<SphereCollider>(),
    std::make_shared<Player>()
  };

  const auto newObject = std::make_shared<Object>(components);
  objectManager->addObject(newObject);
}

void Scene::displayLightGui(const std::shared_ptr<Light>& light, const int id)
{
  glm::vec3 position = light->getPosition();
  glm::vec3 color = light->getColor();
  float ambient = light->getAmbient();
  float diffuse = light->getDiffuse();
  float specular = light->getSpecular();

  ImGui::PushID(id);

  if (ImGui::CollapsingHeader(("Light " + std::to_string(id)).c_str()))
  {
    ImGui::ColorEdit3("Color", value_ptr(color));
    ImGui::SliderFloat("Ambient", &ambient, 0.0f, 1.0f);
    ImGui::SliderFloat("Diffuse", &diffuse, 0.0f, 1.0f);
    ImGui::SliderFloat("Specular", &specular, 0.0f, 1.0f);
    ImGui::SliderFloat3("Position", value_ptr(position), -50.0f, 50.0f);
    ImGui::Separator();
  }

  ImGui::PopID();

  light->setPosition(position);
  light->setColor(color);
  light->setAmbient(ambient);
  light->setDiffuse(diffuse);
  light->setSpecular(specular);
}
