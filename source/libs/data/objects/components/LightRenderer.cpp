#include "LightRenderer.h"
#include "FiniteCheck.h"
#include "WireTypes.h"
#include <nlohmann/json.hpp>
#include <Protocol.h>
#include <algorithm>

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
  if (!finiteCheck::isFinite(color))
  {
    return;
  }

  m_color = color;
}

float LightRenderer::getAmbient() const
{
  return m_ambient;
}

void LightRenderer::setAmbient(const float ambient)
{
  if (!finiteCheck::isFinite(ambient))
  {
    return;
  }

  m_ambient = ambient;
}

float LightRenderer::getDiffuse() const
{
  return m_diffuse;
}

void LightRenderer::setDiffuse(const float diffuse)
{
  if (!finiteCheck::isFinite(diffuse))
  {
    return;
  }

  m_diffuse = diffuse;
}

float LightRenderer::getSpecular() const
{
  return m_specular;
}

void LightRenderer::setSpecular(const float specular)
{
  if (!finiteCheck::isFinite(specular))
  {
    return;
  }

  m_specular = specular;
}

glm::vec3 LightRenderer::getDirection() const
{
  return m_direction;
}

void LightRenderer::setDirection(const glm::vec3& direction)
{
  if (!finiteCheck::isFinite(direction))
  {
    return;
  }

  m_direction = direction;
}

float LightRenderer::getConeAngle() const
{
  return m_coneAngle;
}

void LightRenderer::setConeAngle(const float coneAngle)
{
  // Before the clamp: nan compares false against both bounds, so std::clamp hands it straight back.
  if (!finiteCheck::isFinite(coneAngle))
  {
    return;
  }

  m_coneAngle = std::clamp(coneAngle, minConeAngleDegrees, maxConeAngleDegrees);
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
  setColor(finiteCheck::readVec3OrNaN(componentData.at("color")));
  setDirection(finiteCheck::readVec3OrNaN(componentData.at("direction")));

  setAmbient(finiteCheck::readFloatOrNaN(componentData.at("ambient")));
  setDiffuse(finiteCheck::readFloatOrNaN(componentData.at("diffuse")));
  setSpecular(finiteCheck::readFloatOrNaN(componentData.at("specular")));
  setConeAngle(componentData.at("coneAngle"));

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

  // Every value is read unconditionally so the reader stays aligned; a non-finite one is dropped by its
  // setter, leaving the previous value in place.
  setColor(messageReader.read<glm::vec3>());
  setAmbient(messageReader.read<float>());
  setDiffuse(messageReader.read<float>());
  setSpecular(messageReader.read<float>());

  setDirection(messageReader.read<glm::vec3>());
  setConeAngle(messageReader.read<float>());
}
