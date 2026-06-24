#include "Support.h"
#include <objects/components/collisions/Collider.h>

glm::vec3 getSupport(Collider* collider, const std::shared_ptr<Collider>& other, const glm::vec3& direction)
{
  return collider->findFurthestPoint(direction) - other->findFurthestPoint(-direction);
}
