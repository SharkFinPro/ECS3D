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
  glm::vec3 findFurthestPoint(const glm::vec3& direction) override;
};



#endif //BOXCOLLIDER_H
