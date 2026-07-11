#ifndef BOXCOLLIDER_H
#define BOXCOLLIDER_H

#include "Collider.h"
#include <glm/vec3.hpp>
#include <array>

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


class BoxCollider final : public Collider {
public:
  BoxCollider();

  // Local (collider-offset) accessors for the editor; getPosition/getScale/getRotation below are the
  // world values (transform + offset) the collision system uses.
  [[nodiscard]] glm::vec3 getLocalPosition() const;
  [[nodiscard]] glm::vec3 getLocalScale() const;
  [[nodiscard]] glm::vec3 getLocalRotation() const;

  void setPosition(const glm::vec3& position);
  void setScale(const glm::vec3& scale);
  void setRotation(const glm::vec3& rotation);

  [[nodiscard]] bool getRenderCollider() const;
  void setRenderCollider(bool renderCollider);

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

  [[nodiscard]] glm::vec3 getPosition() override;
  [[nodiscard]] glm::vec3 getScale() override;
  [[nodiscard]] glm::vec3 getRotation() override;

  glm::vec3 findFurthestPoint(const glm::vec3& direction) override;

  void pack(net::Message& message) const override;

  void unpack(net::MessageReader& messageReader) override;

private:
  // Collider gizmo rendering lives in ECS3DRender; m_renderCollider is a plain flag the editor toggles
  // and the render system reads.
  bool m_renderCollider = false;

  std::array<glm::vec3, boxVertices.size()> m_transformedBoxVertices{};

  uint8_t m_currentTransformUpdateID = 255;

  // Editing the collider's own offset doesn't bump the transform's update id, so force a mesh rebuild.
  bool m_meshDirty = true;

  ComponentVariable<glm::vec3> m_position = ComponentVariable(glm::vec3(0));
  ComponentVariable<glm::vec3> m_scale = ComponentVariable(glm::vec3(1));
  ComponentVariable<glm::vec3> m_rotation = ComponentVariable(glm::vec3(0));

  void generateTransformedMesh(const std::shared_ptr<Transform>& transform);

  void updateTransformPointer();
};



#endif //BOXCOLLIDER_H
