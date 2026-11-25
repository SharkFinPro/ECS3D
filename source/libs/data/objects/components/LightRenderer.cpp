#include "LightRenderer.h"
#include "FiniteCheck.h"
#include "WireTypes.h"
#include <nlohmann/json.hpp>
#include <Protocol.h>
#include <algorithm>
#include <string>

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

LightType LightRenderer::getLightType() const
{
  return m_lightType;
}

void LightRenderer::setLightType(const LightType type)
{
  m_lightType = type;
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

float LightRenderer::getRange() const
{
  return m_range;
}

void LightRenderer::setRange(const float range)
{
  if (!finiteCheck::isFinite(range))
  {
    return;
  }

  m_range = range <= 0.0f ? 0.0f : std::max(range, minRange);
}

float LightRenderer::getShadowDistance() const
{
  return m_shadowDistance;
}

void LightRenderer::setShadowDistance(const float distance)
{
  if (!finiteCheck::isFinite(distance))
  {
    return;
  }

  m_shadowDistance = std::clamp(distance, minShadowDistance, maxShadowDistance);
}

namespace {
  const char* lightTypeToString(const LightType type)
  {
    switch (type)
    {
      case LightType::spot:
        return "spot";
      case LightType::directional:
        return "directional";
      case LightType::point:
        break;
    }

    return "point";
  }

  LightType lightTypeFromString(const std::string& name)
  {
    if (name == "point")
    {
      return LightType::point;
    }

    if (name == "spot")
    {
      return LightType::spot;
    }

    if (name == "directional")
    {
      return LightType::directional;
    }

    throw RefusedPayload("LightRenderer: unknown lightType '" + name + "'");
  }
}

nlohmann::json LightRenderer::serialize()
{
  const nlohmann::json data = {
    { "type", "LightRenderer" },
    { "color", { m_color.x, m_color.y, m_color.z } },
    { "direction", { m_direction.x, m_direction.y, m_direction.z } },
    { "lightType", lightTypeToString(m_lightType) },
    { "ambient", m_ambient },
    { "diffuse", m_diffuse },
    { "specular", m_specular },
    { "coneAngle", m_coneAngle },
    { "range", m_range },
    { "shadowDistance", m_shadowDistance }
  };

  return data;
}

void LightRenderer::loadFromJSON(const nlohmann::json& componentData)
{
  LightType type = LightType::point;
  if (componentData.contains("lightType"))
  {
    type = lightTypeFromString(componentData.at("lightType").get<std::string>());
  }
  else if (componentData.contains("isSpotlight"))
  {
    type = componentData.at("isSpotlight").get<bool>() ? LightType::spot : LightType::point;
  }

  const glm::vec3 color = finiteCheck::readVec3OrNaN(componentData.at("color"));
  const glm::vec3 direction = finiteCheck::readVec3OrNaN(componentData.at("direction"));
  const float ambient = finiteCheck::readFloatOrNaN(componentData.at("ambient"));
  const float diffuse = finiteCheck::readFloatOrNaN(componentData.at("diffuse"));
  const float specular = finiteCheck::readFloatOrNaN(componentData.at("specular"));
  const float coneAngle = componentData.at("coneAngle");

  // Files written before range existed keep their unattenuated look.
  float range = 0.0f;
  if (componentData.contains("range"))
  {
    range = finiteCheck::readFloatOrNaN(componentData.at("range"));
  }

  float shadowDistance = 100.0f;
  if (componentData.contains("shadowDistance"))
  {
    shadowDistance = finiteCheck::readFloatOrNaN(componentData.at("shadowDistance"));
  }

  setColor(color);
  setDirection(direction);
  setAmbient(ambient);
  setDiffuse(diffuse);
  setSpecular(specular);
  setConeAngle(coneAngle);
  setRange(range);
  setShadowDistance(shadowDistance);

  m_lightType = type;
}

void LightRenderer::pack(net::Message& message) const
{
  message.write(ComponentType::lightRenderer);

  message.write(static_cast<uint8_t>(m_lightType));

  message.write(m_color);
  message.write(m_ambient);
  message.write(m_diffuse);
  message.write(m_specular);

  message.write(m_direction);
  message.write(m_coneAngle);
  message.write(m_range);
  message.write(m_shadowDistance);
}

void LightRenderer::unpack(net::MessageReader& messageReader)
{
  const auto type = messageReader.read<uint8_t>();
  const auto color = messageReader.read<glm::vec3>();
  const auto ambient = messageReader.read<float>();
  const auto diffuse = messageReader.read<float>();
  const auto specular = messageReader.read<float>();
  const auto direction = messageReader.read<glm::vec3>();
  const auto coneAngle = messageReader.read<float>();
  const auto range = messageReader.read<float>();
  const auto shadowDistance = messageReader.read<float>();

  if (type > static_cast<uint8_t>(LightType::directional))
  {
    throw RefusedPayload("LightRenderer: unknown light type");
  }

  m_lightType = static_cast<LightType>(type);

  // A non-finite value is dropped by its setter, leaving the previous value in place.
  setColor(color);
  setAmbient(ambient);
  setDiffuse(diffuse);
  setSpecular(specular);
  setDirection(direction);
  setConeAngle(coneAngle);
  setRange(range);
  setShadowDistance(shadowDistance);
}
