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

private:
  std::array<glm::vec3, boxVertices.size()> transformedBoxVertices{};

  uint8_t currentTransformUpdateID;

  glm::vec3 findFurthestPoint(const glm::vec3& direction) override;

  void generateTransformedMesh(const std::shared_ptr<Transform>& transform);
};



#endif //BOXCOLLIDER_H
