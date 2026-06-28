#ifndef SPHERECOLLIDER_H
#define SPHERECOLLIDER_H

#include "Collider.h"
#include <glm/vec3.hpp>

class SphereCollider final : public Collider {
public:
  SphereCollider();

  float getRadius();

  // Local (un-scaled) accessors for the editor; getRadius()/getPosition() above are the world values
  // the collision system uses.
  [[nodiscard]] float getLocalRadius() const;
  void setRadius(float radius);

  [[nodiscard]] glm::vec3 getLocalPosition() const;
  void setPosition(const glm::vec3& position);

  [[nodiscard]] bool getRenderCollider() const;
  void setRenderCollider(bool renderCollider);

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

  [[nodiscard]] glm::vec3 getPosition() override;

  glm::vec3 findFurthestPoint(const glm::vec3& direction) override;

  void pack(net::Message& message) const override;

  void unpack(net::MessageReader& messageReader) override;

private:
  bool m_renderCollider = false;

  ComponentVariable<float> m_radius = ComponentVariable(1.0f);

  ComponentVariable<glm::vec3> m_position = ComponentVariable(glm::vec3(0));

  void updateTransformPointer();

  [[nodiscard]] float getScaledRadius(const std::shared_ptr<Transform>& transform);
};



#endif //SPHERECOLLIDER_H
