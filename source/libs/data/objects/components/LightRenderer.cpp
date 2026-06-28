#include "LightRenderer.h"
#include <nlohmann/json.hpp>
#include <Protocol.h>

LightRenderer::LightRenderer()
  : Component(ComponentType::lightRenderer)
{}

LightRenderer::LightRenderer(const glm::vec3 color, const float ambient, const float diffuse, const float specular)
  : Component(ComponentType::lightRenderer),
    m_color(color),
    m_ambient(ambient),
    m_diffuse(diffuse),
    m_specular(specular)
{}

bool LightRenderer::isSpotLight() const
{
  return m_isSpotLight;
}

void LightRenderer::setSpotLight(const bool isSpotLight)
{
  m_isSpotLight = isSpotLight;
}

glm::vec3 LightRenderer::getColor() const
{
  return m_color;
}

void LightRenderer::setColor(const glm::vec3& color)
{
  m_color = color;
}

float LightRenderer::getAmbient() const
{
  return m_ambient;
}

void LightRenderer::setAmbient(const float ambient)
{
  m_ambient = ambient;
}

float LightRenderer::getDiffuse() const
{
  return m_diffuse;
}

void LightRenderer::setDiffuse(const float diffuse)
{
  m_diffuse = diffuse;
}

float LightRenderer::getSpecular() const
{
  return m_specular;
}

void LightRenderer::setSpecular(const float specular)
{
  m_specular = specular;
}

glm::vec3 LightRenderer::getDirection() const
{
  return m_direction;
}

void LightRenderer::setDirection(const glm::vec3& direction)
{
  m_direction = direction;
}

float LightRenderer::getConeAngle() const
{
  return m_coneAngle;
}

void LightRenderer::setConeAngle(const float coneAngle)
{
  m_coneAngle = coneAngle;
}

nlohmann::json LightRenderer::serialize()
{
  const nlohmann::json data = {
    { "type", "LightRenderer" },
    { "color", { m_color.x, m_color.y, m_color.z } },
    { "direction", { m_direction.x, m_direction.y, m_direction.z } },
    { "isSpotlight", m_isSpotLight },
    { "ambient", m_ambient },
    { "diffuse", m_diffuse },
    { "specular", m_specular },
    { "coneAngle", m_coneAngle }
  };

  return data;
}

void LightRenderer::loadFromJSON(const nlohmann::json& componentData)
{
  const auto& color = componentData.at("color");
  m_color = glm::vec3(color.at(0), color.at(1), color.at(2));

  const auto& direction = componentData.at("direction");
  m_direction = glm::vec3(direction.at(0), direction.at(1), direction.at(2));

  m_ambient = componentData.at("ambient");
  m_diffuse = componentData.at("diffuse");
  m_specular = componentData.at("specular");
  m_coneAngle = componentData.at("coneAngle");

  m_isSpotLight = componentData.at("isSpotlight");
}

void LightRenderer::pack(net::Message& message) const
{
  message.write(ComponentType::lightRenderer);

  message.write(m_isSpotLight);

  message.write(m_color);
  message.write(m_ambient);
  message.write(m_diffuse);
  message.write(m_specular);

  message.write(m_direction);
  message.write(m_coneAngle);
}

void LightRenderer::unpack(net::MessageReader& messageReader)
{
  m_isSpotLight = messageReader.read<bool>();

  m_color = messageReader.read<glm::vec3>();
  m_ambient = messageReader.read<float>();
  m_diffuse = messageReader.read<float>();
  m_specular = messageReader.read<float>();

  m_direction = messageReader.read<glm::vec3>();
  m_coneAngle = messageReader.read<float>();
}
