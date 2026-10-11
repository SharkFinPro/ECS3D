#ifndef CAPSULECOLLIDER_H
#define CAPSULECOLLIDER_H

#include "Collider.h"
#include <glm/vec3.hpp>
#include <cstdint>

struct CapsuleSegment {
  glm::vec3 start{};
  glm::vec3 end{};
};

// A capsule whose axis is local +Y. The height is the total height including both caps; the core segment
// the caps are swept around has half-length max(height / 2 - radius, 0) in world units.
class CapsuleCollider final : public Collider {
public:
  CapsuleCollider();

  // Local (un-scaled) accessors for the editor; getRadius/getSegment/getPosition/getRotation below are the
  // world values the collision system uses.
  [[nodiscard]] glm::vec3 getLocalPosition() const;
  [[nodiscard]] glm::vec3 getLocalRotation() const;
  [[nodiscard]] float getLocalRadius() const;
  [[nodiscard]] float getLocalHeight() const;

  void setPosition(const glm::vec3& position);
  void setRotation(const glm::vec3& rotation);
  void setRadius(float radius);
  void setHeight(float height);

  [[nodiscard]] float getRadius();

  // The two world-space ends of the core segment; each cap's sphere is centered on one of them.
  [[nodiscard]] CapsuleSegment getSegment();

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

  [[nodiscard]] glm::vec3 getPosition() override;
  [[nodiscard]] glm::vec3 getRotation() override;

  glm::vec3 findFurthestPoint(const glm::vec3& direction) override;

  void pack(net::Message& message) const override;

  void unpack(net::MessageReader& messageReader) override;

  // The live and authored values differ, so switching between them changes the cached world shape.
  void start() override;

  void stop() override;

private:
  // Segment ends and radius in world space, rebuilt when the transform's update id moves or a setter, load,
  // unpack, start or stop changes the capsule's own fields.
  struct WorldShape {
    CapsuleSegment segment;
    float radius = 0.0f;
  };

  WorldShape m_worldShape;
  uint64_t m_worldShapeUpdateID = 0;
  bool m_worldShapeDirty = true;

  ComponentVariable<glm::vec3> m_position = ComponentVariable(glm::vec3(0));
  ComponentVariable<glm::vec3> m_rotation = ComponentVariable(glm::vec3(0));
  ComponentVariable<float> m_radius = ComponentVariable(0.5f);
  ComponentVariable<float> m_height = ComponentVariable(2.0f);

  void updateTransformPointer();

  void invalidateShape();

  [[nodiscard]] const WorldShape& worldShape();

  [[nodiscard]] float getScaledRadius(const std::shared_ptr<Transform>& transform) const;
  [[nodiscard]] float getSegmentHalfLength(const std::shared_ptr<Transform>& transform) const;
  [[nodiscard]] glm::vec3 getWorldAxis(const std::shared_ptr<Transform>& transform) const;
};



#endif //CAPSULECOLLIDER_H
