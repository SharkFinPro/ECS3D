#include "Transform.h"

Transform::Transform(const glm::vec3& position)
  : Component(ComponentType::transform), initialPosition(position), position(initialPosition)
{}

glm::vec3 Transform::getPosition() const
{
  return position;
}

void Transform::move(const glm::vec3& direction)
{
  position += direction;
}

void Transform::reset()
{
  position = initialPosition;
}
