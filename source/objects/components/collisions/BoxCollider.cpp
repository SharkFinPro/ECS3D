#include "BoxCollider.h"
#include "../../Object.h"
#include "../Transform.h"
#include <stdexcept>
#include <glm/glm.hpp>
#include <limits>

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

  float furthestDistance = std::numeric_limits<float>::lowest();
  glm::vec3 furthestVertex{ 0, 0, 0 };

  if (const std::shared_ptr<Transform> transform = transform_ptr.lock())
  {
    const auto scale = transform->getScale();
    const auto position = transform->getPosition();

    for (auto& vertex : boxVertices)
    {
      const glm::vec3 transformedVertex = vertex * scale + position;
      if (const float distance = dot(transformedVertex, direction); distance > furthestDistance)
      {
        furthestDistance = distance;
        furthestVertex = transformedVertex;
      }
    }
  }

  return furthestVertex;
}
