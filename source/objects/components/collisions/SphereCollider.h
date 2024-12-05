#ifndef SPHERECOLLIDER_H
#define SPHERECOLLIDER_H

#include "Collider.h"
#include <glm/vec3.hpp>

class SphereCollider final : public Collider {
public:
  SphereCollider();

  float getRadius();

private:
  float radius = 1.0f;

  glm::vec3 findFurthestPoint(const glm::vec3& direction) override;
};



#endif //SPHERECOLLIDER_H
