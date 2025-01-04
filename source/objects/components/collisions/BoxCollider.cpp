#include "BoxCollider.h"
#include "../../Object.h"
#include "../Transform.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <limits>

BoxCollider::BoxCollider()
  : Collider(ColliderType::boxCollider, ComponentType::SubComponentType_boxCollider), currentTransformUpdateID(255)
{}

void BoxCollider::displayGui()
{
  if (displayGuiHeader())
  {}
}

glm::vec3 BoxCollider::findFurthestPoint(const glm::vec3& direction)
{
  if (transform_ptr.expired())
  {
    transform_ptr = owner->getComponent<Transform>(ComponentType::transform);

    if (transform_ptr.expired())
    {
      throw std::runtime_error("MeshCollider::findFurthestPoint::Missing transform component");
    }
  }

  if (const std::shared_ptr<Transform> transform = transform_ptr.lock())
  {
    if (currentTransformUpdateID != transform->getUpdateID())
    {
      generateTransformedMesh(transform);
    }
  }

  float largestDot = std::numeric_limits<float>::lowest();
  glm::vec3 furthestVertex{ 0, 0, 0 };

  for (auto& vertex : transformedBoxVertices)
  {
    if (const float currentDot = dot(vertex, direction); currentDot > largestDot)
    {
      largestDot = currentDot;
      furthestVertex = vertex;
    }
  }

  return furthestVertex;
}

void BoxCollider::generateTransformedMesh(const std::shared_ptr<Transform>& transform)
{
  const auto rotation = transform->getRotation();
  const auto scale = transform->getScale();
  const auto position = transform->getPosition();

  const auto transformationMatrix = translate(glm::mat4(1.0f), position)
    * rotate(glm::mat4(1.0f), glm::radians(rotation.z), {0, 0, 1})
    * rotate(glm::mat4(1.0f), glm::radians(rotation.y), {0, 1, 0})
    * rotate(glm::mat4(1.0f), glm::radians(rotation.x), {1, 0, 0})
    * glm::scale(glm::mat4(1.0f), scale);

  for (size_t i = 0; i < boxVertices.size(); ++i)
  {
    const auto transformedVertex = transformationMatrix * glm::vec4(boxVertices[i], 1.0f);

    transformedBoxVertices[i] = glm::vec3(transformedVertex);
  }

  currentTransformUpdateID = transform->getUpdateID();
}
