#include "Transform.h"

Transform::Transform(const glm::vec3& position, const glm::vec3& scale)
  : Component(ComponentType::transform), initialPosition(position), position(initialPosition),
    initialScale(scale), scale(initialScale)
{}

glm::vec3 Transform::getPosition() const
{
  return position;
}

glm::vec3 Transform::getScale() const
{
  return scale;
}

void Transform::move(const glm::vec3& direction)
{
  position += direction;
}

void Transform::reset()
{
  position = initialPosition;
}
