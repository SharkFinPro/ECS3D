#include "BoxCollider.h"
#include "../../Object.h"
#include "../Transform.h"
#include <stdexcept>
#include <glm/glm.hpp>
#include <limits>

#include "glm/gtc/matrix_transform.hpp"

BoxCollider::BoxCollider()
  : Collider(ColliderType::boxCollider)
{}

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
    const auto rotation = transform->getRotation();
    const auto scale = transform->getScale();
    const auto position = transform->getPosition();

    const auto transformationMatrix = translate(glm::mat4(1.0f), position)
        * rotate(glm::mat4(1.0f), glm::radians(rotation.z), {0, 0, 1}) // Z-axis rotation
        * rotate(glm::mat4(1.0f), glm::radians(rotation.y), {0, 1, 0}) // Y-axis rotation
        * rotate(glm::mat4(1.0f), glm::radians(rotation.x), {1, 0, 0}) // X-axis rotation
        * glm::scale(glm::mat4(1.0f), scale);

    for (auto& vertex : boxVertices)
    {
      const auto transformedVertex4 = transformationMatrix * glm::vec4(vertex, 1.0f);
      const auto transformedVertex = glm::vec3(transformedVertex4);

      if (const float distance = dot(transformedVertex, direction); distance > furthestDistance)
      {
        furthestDistance = distance;
        furthestVertex = transformedVertex;
      }
    }
  }

  return furthestVertex;
}
