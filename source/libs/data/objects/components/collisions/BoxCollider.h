#ifndef BOXCOLLIDER_H
#define BOXCOLLIDER_H

#include "Collider.h"
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <array>
#include <cstdint>
#include <limits>

constexpr std::array<glm::vec3, 8> boxVertices = {{
  {1.0f, 1.0f, 1.0f},
  {-1.0f, 1.0f, 1.0f},
  {-1.0f, 1.0f, -1.0f},
  {1.0f, 1.0f, -1.0f},
  {1.0f, -1.0f, 1.0f},
  {-1.0f, -1.0f, 1.0f},
  {-1.0f, -1.0f, -1.0f},
  {1.0f, -1.0f, -1.0f}
}};

// A box's world-space corners.
struct BoxSupport {
  std::array<glm::vec3, boxVertices.size()> vertices{};

  // A full scan rather than picking the corner from the signs of the axis dots: the two break ties (a
  // direction square to a face) toward different corners, and the response is tuned on the scan's choice.
  [[nodiscard]] glm::vec3 furthestPoint(const glm::vec3& direction) const
  {
    float largestDot = std::numeric_limits<float>::lowest();
    glm::vec3 furthestVertex{ 0, 0, 0 };

    for (const auto& vertex : vertices)
    {
      if (const float currentDot = glm::dot(vertex, direction); currentDot > largestDot)
      {
        largestDot = currentDot;
        furthestVertex = vertex;
      }
    }

    return furthestVertex;
  }
};

// The box as the contact manifold reads it: world center, unit local axes and the half extent along each.
struct BoxWorldFrame {
  glm::vec3 center{ 0.0f };
  std::array<glm::vec3, 3> axes{};
  std::array<float, 3> halfExtents{};
};


class BoxCollider final : public Collider {
public:
  BoxCollider();

  // Local (collider-offset) accessors for the editor; getPosition/getScale/getRotation below are the
  // world values the collision system uses - position and rotation offset the transform's, scale
  // multiplies it, matching the matrix the collision mesh is built with.
  [[nodiscard]] glm::vec3 getLocalPosition() const;
  [[nodiscard]] glm::vec3 getLocalScale() const;
  [[nodiscard]] glm::vec3 getLocalRotation() const;

  void setPosition(const glm::vec3& position);
  void setScale(const glm::vec3& scale);
  void setRotation(const glm::vec3& rotation);

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

  [[nodiscard]] glm::vec3 getPosition() override;
  [[nodiscard]] glm::vec3 getScale() override;
  [[nodiscard]] glm::vec3 getRotation() override;

  glm::vec3 findFurthestPoint(const glm::vec3& direction) override;

  // The geometry findFurthestPoint reads, refreshed first.
  [[nodiscard]] const BoxSupport& getWorldSupport();

  // Built from getPosition, getRotation and getScale, and kept until the transform or the box changes.
  [[nodiscard]] const BoxWorldFrame& getWorldFrame();

  void pack(net::Message& message) const override;

  void unpack(net::MessageReader& messageReader) override;

private:
  BoxSupport m_worldSupport;

  BoxWorldFrame m_worldFrame;
  uint64_t m_worldFrameUpdateID = 0;
  uint64_t m_worldFrameRevision = 0;
  bool m_worldFrameValid = false;

  uint64_t m_currentTransformUpdateID = 255;

  // Editing the collider's own offset doesn't bump the transform's update id, so force a mesh rebuild.
  bool m_meshDirty = true;

  ComponentVariable<glm::vec3> m_position = ComponentVariable(glm::vec3(0));
  ComponentVariable<glm::vec3> m_scale = ComponentVariable(glm::vec3(1));
  ComponentVariable<glm::vec3> m_rotation = ComponentVariable(glm::vec3(0));

  void generateTransformedMesh(const Transform& transform);

  void updateTransformPointer();
};



#endif //BOXCOLLIDER_H
