#include "LightRenderer.h"
#include "Transform.h"
#include "../Object.h"
#include "../ObjectManager.h"
#include "../../ECS3D.h"
#include "glm/gtc/type_ptr.hpp"

LightRenderer::LightRenderer(glm::vec3 position, glm::vec3 color, float ambient, float diffuse, float specular)
  : Component(ComponentType::lightRenderer), light(std::make_shared<Light>(position, color, ambient, diffuse, specular))
{}

void LightRenderer::variableUpdate([[maybe_unused]] const float dt)
{
  if (transform_ptr.expired())
  {
    transform_ptr = std::dynamic_pointer_cast<Transform>(owner->getComponent(ComponentType::transform));

    if (transform_ptr.expired())
    {
      return;
    }
  }

  if (const std::shared_ptr<Transform> transform = transform_ptr.lock())
  {
    light->setPosition(transform->getPosition());
  }

  owner->getManager()->getECS()->getRenderer()->renderLight(light);
}

void LightRenderer::displayGui() const
{
  glm::vec3 color = light->getColor();
  float ambient = light->getAmbient();
  float diffuse = light->getDiffuse();
  float specular = light->getSpecular();

  ImGui::ColorEdit3("Color", value_ptr(color));
  ImGui::SliderFloat("Ambient", &ambient, 0.0f, 1.0f);
  ImGui::SliderFloat("Diffuse", &diffuse, 0.0f, 1.0f);
  ImGui::SliderFloat("Specular", &specular, 0.0f, 1.0f);
  ImGui::Separator();

  light->setColor(color);
  light->setAmbient(ambient);
  light->setDiffuse(diffuse);
  light->setSpecular(specular);
}
