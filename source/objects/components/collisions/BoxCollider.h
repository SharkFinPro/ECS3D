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

  void displayGui() override;

  [[nodiscard]] nlohmann::json serialize() override;

private:
  std::array<glm::vec3, boxVertices.size()> m_transformedBoxVertices{};

  uint8_t m_currentTransformUpdateID = 255;

  glm::vec3 findFurthestPoint(const glm::vec3& direction) override;

  void generateTransformedMesh(const std::shared_ptr<Transform>& transform);
};



#endif //BOXCOLLIDER_H
