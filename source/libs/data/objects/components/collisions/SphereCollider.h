#ifndef SPHERECOLLIDER_H
#define SPHERECOLLIDER_H

#include "Collider.h"
#include <glm/vec3.hpp>
#include <cstdint>

// A sphere's world placement: the transform's world position, the collider's own offset and the world radius.
// The two positions stay apart so the support point sums them in the order it always has.
struct SphereSupport {
  glm::vec3 transformPosition{ 0 };
  glm::vec3 offset{ 0 };
  float radius = 0;

  [[nodiscard]] glm::vec3 furthestPoint(const glm::vec3& direction) const
  {
    return direction * radius + transformPosition + offset;
  }
};

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

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

  [[nodiscard]] glm::vec3 getPosition() override;

  glm::vec3 findFurthestPoint(const glm::vec3& direction) override;

  // The geometry findFurthestPoint reads, refreshed first.
  [[nodiscard]] const SphereSupport& getWorldSupport();

  void pack(net::Message& message) const override;

  void unpack(net::MessageReader& messageReader) override;

private:
  ComponentVariable<float> m_radius = ComponentVariable(1.0f);

  ComponentVariable<glm::vec3> m_position = ComponentVariable(glm::vec3(0));

  // World center and radius for the support query; the transform's world getters walk the parent chain.
  SphereSupport m_worldSupport;
  uint64_t m_currentTransformUpdateID = 0;

  // Editing the collider's own offset or radius doesn't bump the transform's update id.
  bool m_shapeDirty = true;

  void updateTransformPointer();

  [[nodiscard]] float getScaledRadius(const std::shared_ptr<Transform>& transform);
};



#endif //SPHERECOLLIDER_H
