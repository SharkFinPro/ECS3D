#ifndef LIGHTRENDERER_H
#define LIGHTRENDERER_H

#include "Component.h"
#include <glm/vec3.hpp>

class LightRenderer final : public Component {
public:
  LightRenderer();

  LightRenderer(glm::vec3 color, float ambient, float diffuse, float specular);

  [[nodiscard]] bool isSpotLight() const;
  void setSpotLight(bool isSpotLight);

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

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

  void pack(net::Message& message) const override;

  void unpack(net::MessageReader& messageReader) override;

private:
  bool m_isSpotLight = false;

  // A light added in the editor should light something. Black with no diffuse or specular contributed
  // nothing until all four were changed by hand; these match the point lights the default project uses.
  glm::vec3 m_color = glm::vec3(1.0f);
  float m_ambient = 0.0f;
  float m_diffuse = 0.75f;
  float m_specular = 0.75f;

  glm::vec3 m_direction = glm::vec3(0, -1, 0);
  float m_coneAngle = 0.0f;
};



#endif //LIGHTRENDERER_H
