#ifndef LIGHTRENDERER_H
#define LIGHTRENDERER_H

#include "Component.h"
#include <glm/vec3.hpp>
#include <cstdint>

enum class LightType : uint8_t
{
  point,
  spot,
  directional
};

class LightRenderer final : public Component {
public:
  LightRenderer();

  LightRenderer(glm::vec3 color, float ambient, float diffuse, float specular);

  [[nodiscard]] LightType getLightType() const;
  void setLightType(LightType type);

  [[nodiscard]] glm::vec3 getColor() const;
  void setColor(const glm::vec3& color);

  [[nodiscard]] float getAmbient() const;
  void setAmbient(float ambient);

  [[nodiscard]] float getDiffuse() const;
  void setDiffuse(float diffuse);

  [[nodiscard]] float getSpecular() const;
  void setSpecular(float specular);

  [[nodiscard]] glm::vec3 getDirection() const;
  void setDirection(const glm::vec3& direction);

  [[nodiscard]] float getConeAngle() const;
  void setConeAngle(float coneAngle);

  // The shadow frustum's fov is coneAngle * 2, so a value at or past 90 projects at or past 180 degrees
  // and degenerates. Shared by the editor slider and the setter's own clamp.
  static constexpr float minConeAngleDegrees = 1.0f;
  static constexpr float maxConeAngleDegrees = 89.0f;

  // Range 0 means unbounded and unattenuated. A positive range below minRange is raised to it, since the
  // renderer rejects anything near zero.
  [[nodiscard]] float getRange() const;
  void setRange(float range);

  static constexpr float minRange = 0.5f;

  // How far from the camera a directional light casts shadows. Unused by point and spot lights.
  [[nodiscard]] float getShadowDistance() const;
  void setShadowDistance(float distance);

  static constexpr float minShadowDistance = 1.0f;
  static constexpr float maxShadowDistance = 1000.0f;

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

  void pack(net::Message& message) const override;

  void unpack(net::MessageReader& messageReader) override;

private:
  LightType m_lightType = LightType::point;

  // Matches the default project's point lights, so a light added in the editor lights something. Ambient
  // stays 0 on purpose: it is an unattenuated flat fill, so only one light in a scene should carry it.
  glm::vec3 m_color = glm::vec3(1.0f);
  float m_ambient = 0.0f;
  float m_diffuse = 0.75f;
  float m_specular = 0.75f;

  // Read by spot and directional lights, where a zero direction points nowhere and a zero cone
  // angle closes the cone entirely. The angle is in degrees and doubles into the shadow frustum's fov.
  glm::vec3 m_direction = glm::vec3(0, -1, 0);
  float m_coneAngle = 30.0f;

  // Default 10 keeps a light added in the editor local rather than a second sun.
  float m_range = 10.0f;
  float m_shadowDistance = 100.0f;
};



#endif //LIGHTRENDERER_H
