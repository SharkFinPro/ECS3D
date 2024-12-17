#include "Transform.h"

Transform::Transform(const glm::vec3& position, const glm::vec3& scale, const glm::vec3& rotation)
  : Component(ComponentType::transform), initialPosition(position), position(initialPosition),
    initialScale(scale), scale(initialScale), initialRotation(rotation), rotation(initialRotation)
{}

glm::vec3 Transform::getPosition() const
{
  return position;
}

glm::vec3 Transform::getScale() const
{
  return scale;
}

glm::vec3 Transform::getRotation() const
{
  return rotation;
}

void Transform::setScale(const glm::vec3 scale)
{
  this->scale = scale;
}

void Transform::setRotation(const glm::vec3 rotation)
{
  this->rotation = rotation;
}

void Transform::move(const glm::vec3& direction)
{
  position += direction;
}

void Transform::reset()
{
  position = initialPosition;
  scale = initialScale;
  rotation = initialRotation;
}
