#ifndef SPHERECOLLIDER_H
#define SPHERECOLLIDER_H

#include "Collider.h"
#include <glm/vec3.hpp>

class SphereCollider final : public Collider {
public:
  SphereCollider();

  float getRadius();

  void displayGui() override;

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

private:
  float m_radius = 1.0f;

  glm::vec3 findFurthestPoint(const glm::vec3& direction) override;
};



#endif //SPHERECOLLIDER_H
