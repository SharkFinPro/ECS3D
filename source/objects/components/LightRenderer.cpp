#include "LightRenderer.h"
#include "Transform.h"
#include "../Object.h"
#include "../ObjectManager.h"
#include "../../ECS3D.h"
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <VulkanEngine/components/lighting/LightingManager.h>

LightRenderer::LightRenderer(const std::shared_ptr<vke::VulkanEngine>& renderer)
  : Component(ComponentType::lightRenderer)
{
  const auto lightingManager = renderer->getLightingManager();

  m_pointLight = std::dynamic_pointer_cast<vke::PointLight>(lightingManager->createPointLight(glm::vec3(0), glm::vec3(0), 0, 0, 0));

  m_spotLight = std::dynamic_pointer_cast<vke::SpotLight>(lightingManager->createSpotLight(glm::vec3(0), glm::vec3(0), 0, 0, 0));
}

LightRenderer::LightRenderer(const std::shared_ptr<vke::VulkanEngine>& renderer,
                             const glm::vec3 color,
                             const float ambient,
                             const float diffuse,
                             const float specular)
  : Component(ComponentType::lightRenderer)
{
  const auto lightingManager = renderer->getLightingManager();

  m_pointLight = std::dynamic_pointer_cast<vke::PointLight>(lightingManager->createPointLight(glm::vec3(0), color, ambient, diffuse, specular));

  m_spotLight = std::dynamic_pointer_cast<vke::SpotLight>(lightingManager->createSpotLight(glm::vec3(0), color, ambient, diffuse, specular));
}

void LightRenderer::variableUpdate([[maybe_unused]] const float dt)
{
  if (m_transform_ptr.expired())
  {
    m_transform_ptr = m_owner->getComponent<Transform>(ComponentType::transform);

    if (m_transform_ptr.expired())
    {
      return;
    }
  }

  if (const std::shared_ptr<Transform> transform = m_transform_ptr.lock())
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
    m_owner->getManager()->getECS()->getRenderer()->getLightingManager()->renderLight(m_spotLight);
  }
  else
  {
    m_owner->getManager()->getECS()->getRenderer()->getLightingManager()->renderLight(m_pointLight);
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

nlohmann::json LightRenderer::serialize()
{
  const auto light = m_spotLight;

  const auto color = light->getColor();
  const auto direction = light->getDirection();

  const nlohmann::json data = {
    { "type", "LightRenderer" },
    { "color", { color.x, color.y, color.z } },
    { "direction", { direction.x, direction.y, direction.z } },
    { "isSpotlight", m_isSpotLight },
    { "ambient", light->getAmbient() },
    { "diffuse", light->getDiffuse() },
    { "specular", light->getSpecular() },
    { "coneAngle", light->getConeAngle() }
  };

  return data;
}

void LightRenderer::loadFromJSON(const nlohmann::json& componentData)
{
  const auto& color = componentData.at("color");
  m_spotLight->setColor(glm::vec3(
    color.at(0),
    color.at(1),
    color.at(2)
  ));

  const auto& direction = componentData.at("direction");
  m_spotLight->setDirection(glm::vec3(
    direction.at(0),
    direction.at(1),
    direction.at(2)
  ));

  m_spotLight->setAmbient(componentData.at("ambient"));
  m_spotLight->setDiffuse(componentData.at("diffuse"));
  m_spotLight->setSpecular(componentData.at("specular"));
  m_spotLight->setConeAngle(componentData.at("coneAngle"));

  m_pointLight->setColor(m_spotLight->getColor());
  m_pointLight->setAmbient(m_spotLight->getAmbient());
  m_pointLight->setDiffuse(m_spotLight->getDiffuse());
  m_pointLight->setSpecular(m_spotLight->getSpecular());

  m_isSpotLight = componentData.at("isSpotlight");
}
