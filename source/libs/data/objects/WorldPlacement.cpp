#include "WorldPlacement.h"
#include "Object.h"
#include "components/RotationConvention.h"
#include "components/Transform.h"
#include <glm/gtc/quaternion.hpp>
#include <cmath>

std::optional<WorldPlacement> captureWorldPlacement(const std::shared_ptr<Object>& object)
{
  const auto transform = object->getComponent<Transform>(ComponentType::transform);
  if (!transform)
  {
    return std::nullopt;
  }

  return WorldPlacement{ transform->getPosition(), transform->getRotation(), transform->getScale() };
}

void restoreWorldPlacement(const std::shared_ptr<Object>& object,
                           const std::shared_ptr<Object>& newParent,
                           const WorldPlacement& placement)
{
  const auto transform = object->getComponent<Transform>(ComponentType::transform);
  if (!transform)
  {
    return;
  }

  glm::vec3 parentPosition(0.0f);
  glm::quat parentOrientation(1.0f, 0.0f, 0.0f, 0.0f);
  glm::vec3 parentScale(1.0f);
  bool hasParentTransform = false;

  if (newParent)
  {
    if (const auto parentTransform = newParent->getComponent<Transform>(ComponentType::transform))
    {
      hasParentTransform = true;
      parentPosition = parentTransform->getPosition();
      parentOrientation = parentTransform->getOrientation();
      parentScale = parentTransform->getScale();
    }
  }

  // An axis whose compensated value is not representable (the parent's world scale there is zero, denormal
  // enough to overflow the division, or the division otherwise yields inf/nan) keeps the value it already
  // had rather than writing one that would break every reader of this transform.
  auto localScale = transform->getLocalScale();
  for (int axis = 0; axis < 3; ++axis)
  {
    if (const auto compensated = placement.scale[axis] / parentScale[axis]; std::isfinite(compensated))
    {
      localScale[axis] = compensated;
    }
  }

  auto localPosition = transform->getLocalPosition();
  const auto offset = glm::inverse(parentOrientation) * (placement.position - parentPosition);
  for (int axis = 0; axis < 3; ++axis)
  {
    if (const auto compensated = offset[axis] / parentScale[axis]; std::isfinite(compensated))
    {
      localPosition[axis] = compensated;
    }
  }

  const auto localRotation = hasParentTransform
    ? quatToEulerDegrees(glm::inverse(parentOrientation) * eulerDegreesToQuat(placement.rotation))
    : placement.rotation;

  transform->setPosition(localPosition);
  transform->setRotation(localRotation);
  transform->setScale(localScale);
}
