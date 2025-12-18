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

  m_pointLight = std::dynamic_pointer_cast<vke::PointLight>(a->createPointLight(position, color, ambient, diffuse, specular));
  m_spotLight = std::dynamic_pointer_cast<vke::SpotLight>(a->createSpotLight(position, color, ambient, diffuse, specular));
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
    if (m_isSpotLight)
    {
      m_spotLight->setPosition(transform->getPosition());
    }
    else
    {
      m_pointLight->setPosition(transform->getPosition());
    }

  }

  if (m_isSpotLight)
  {
    owner->getManager()->getECS()->getRenderer()->getLightingManager()->renderLight(m_spotLight);
  }
  else
  {
    owner->getManager()->getECS()->getRenderer()->getLightingManager()->renderLight(m_pointLight);
  }
}

void LightRenderer::displayGui()
{
  if (displayGuiHeader())
  {
    bool isSpotLight = m_isSpotLight;
    glm::vec3 color = m_spotLight->getColor();
    float ambient = m_spotLight->getAmbient();
    float diffuse = m_spotLight->getDiffuse();
    float specular = m_spotLight->getSpecular();
    glm::vec3 direction = m_spotLight->getDirection();
    float coneAngle = m_spotLight->getConeAngle();

    ImGui::Checkbox("Spot Light", &isSpotLight);
    ImGui::ColorEdit3("Color", value_ptr(color));
    ImGui::SliderFloat("Ambient", &ambient, 0.0f, 1.0f);
    ImGui::SliderFloat("Diffuse", &diffuse, 0.0f, 1.0f);
    ImGui::SliderFloat("Specular", &specular, 0.0f, 1.0f);
    ImGui::SliderFloat3("Direction", value_ptr(direction), -1.0f, 1.0f);
    ImGui::SliderFloat("Cone Angle", &coneAngle, 0.0f, 180.0f);

    if (isSpotLight != m_isSpotLight)
    {
      m_isSpotLight = !m_isSpotLight;
    }

    m_pointLight->setColor(color);
    m_pointLight->setAmbient(ambient);
    m_pointLight->setDiffuse(diffuse);
    m_pointLight->setSpecular(specular);

    m_spotLight->setColor(color);
    m_spotLight->setAmbient(ambient);
    m_spotLight->setDiffuse(diffuse);
    m_spotLight->setSpecular(specular);
    m_spotLight->setDirection(direction);
    m_spotLight->setConeAngle(coneAngle);
  }
}
