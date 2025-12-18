#include "LightRenderer.h"
#include "Transform.h"
#include "../Object.h"
#include "../ObjectManager.h"
#include "../../ECS3D.h"
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <VulkanEngine/components/lighting/LightingManager.h>

LightRenderer::LightRenderer(std::shared_ptr<vke::VulkanEngine> renderer, glm::vec3 position, glm::vec3 color, float ambient, float diffuse, float specular)
  : Component(ComponentType::lightRenderer)//, light(renderer->getAstd::make_shared<vke::PointLight>(position, color, ambient, diffuse, specular))
{
  auto a = renderer->getLightingManager();

  light = std::dynamic_pointer_cast<vke::PointLight>(a->createPointLight(position, color, ambient, diffuse, specular));
}

void LightRenderer::variableUpdate([[maybe_unused]] const float dt)
{
  if (transform_ptr.expired())
  {
    transform_ptr = owner->getComponent<Transform>(ComponentType::transform);

    if (transform_ptr.expired())
    {
      return;
    }
  }

  if (const std::shared_ptr<Transform> transform = transform_ptr.lock())
  {
    light->setPosition(transform->getPosition());
  }

  owner->getManager()->getECS()->getRenderer()->getLightingManager()->renderLight(light);
}

void LightRenderer::displayGui()
{
  if (displayGuiHeader())
  {
    // bool isSpotLight = light->isSpotLight();
    glm::vec3 color = light->getColor();
    float ambient = light->getAmbient();
    float diffuse = light->getDiffuse();
    float specular = light->getSpecular();
    // glm::vec3 direction = light->getDirection();
    // float coneAngle = light->getConeAngle();

    // ImGui::Checkbox("Spot Light", &isSpotLight);
    ImGui::ColorEdit3("Color", value_ptr(color));
    ImGui::SliderFloat("Ambient", &ambient, 0.0f, 1.0f);
    ImGui::SliderFloat("Diffuse", &diffuse, 0.0f, 1.0f);
    ImGui::SliderFloat("Specular", &specular, 0.0f, 1.0f);
    // ImGui::SliderFloat3("Direction", value_ptr(direction), -1.0f, 1.0f);
    // ImGui::SliderFloat("Cone Angle", &coneAngle, 0.0f, 180.0f);

    // light->setSpotLight(isSpotLight);
    light->setColor(color);
    light->setAmbient(ambient);
    light->setDiffuse(diffuse);
    light->setSpecular(specular);
    // light->setDirection(direction);
    // light->setConeAngle(coneAngle);
  }
}
