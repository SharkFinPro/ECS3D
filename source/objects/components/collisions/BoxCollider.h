#ifndef BOXCOLLIDER_H
#define BOXCOLLIDER_H

#include "Collider.h"
#include <glm/vec3.hpp>
#include <VulkanEngine/components/assets/objects/RenderObject.h>
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

  void displayGui() override;

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

  void variableUpdate(float dt) override;

  [[nodiscard]] glm::vec3 getPosition() override;
  [[nodiscard]] glm::vec3 getScale() override;
  [[nodiscard]] glm::vec3 getRotation() override;

private:
  std::shared_ptr<vke::RenderObject> m_renderObject = nullptr;

  bool m_renderCollider = false;

  std::array<glm::vec3, boxVertices.size()> m_transformedBoxVertices{};

  uint8_t m_currentTransformUpdateID = 255;

  ComponentVariable<glm::vec3> m_position = ComponentVariable(glm::vec3(0));
  ComponentVariable<glm::vec3> m_scale = ComponentVariable(glm::vec3(1));
  ComponentVariable<glm::vec3> m_rotation = ComponentVariable(glm::vec3(0));

  glm::vec3 findFurthestPoint(const glm::vec3& direction) override;

  void generateTransformedMesh(const std::shared_ptr<Transform>& transform);

  void updateTransformPointer();
};



#endif //BOXCOLLIDER_H
