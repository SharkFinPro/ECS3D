#include "BoxCollider.h"
#include "../../Object.h"
#include "../Transform.h"
#include <stdexcept>
#include <glm/glm.hpp>

glm::vec3 BoxCollider::findFurthestPoint(const glm::vec3& direction)
{
  if (transform_ptr.expired())
  {
    transform_ptr = std::dynamic_pointer_cast<Transform>(owner->getComponent(ComponentType::transform));

    if (transform_ptr.expired())
    {
      throw std::runtime_error("MeshCollider::findFurthestPoint::Missing transform component");
    }
  }

  float furthestDistance = -FLT_MAX;
  glm::vec3 furthestVertex{ 0, 0, 0 };

  if (const std::shared_ptr<Transform> transform = transform_ptr.lock())
  {
    std::array<glm::vec3, 8> mesh{};
    for (int i = 0; i < 8; i++)
    {
      mesh[i] = boxVertices[i] * transform->getScale() + transform->getPosition();
    }

    for (auto& vertex : mesh)
    {
      if (const float distance = dot(vertex, direction); distance > furthestDistance)
      {
        furthestDistance = distance;
        furthestVertex = vertex;
      }
    }
  }

  return furthestVertex;
}
