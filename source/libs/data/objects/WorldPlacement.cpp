#include "WorldPlacement.h"
#include "Object.h"
#include "components/Transform.h"
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
  glm::vec3 parentRotation(0.0f);
  glm::vec3 parentScale(1.0f);

  if (newParent)
  {
    if (const auto parentTransform = newParent->getComponent<Transform>(ComponentType::transform))
    {
      parentPosition = parentTransform->getPosition();
      parentRotation = parentTransform->getRotation();
      parentScale = parentTransform->getScale();
    }
  }

  auto localScale = transform->getLocalScale();
  for (int axis = 0; axis < 3; ++axis)
  {
    // An axis whose compensated scale is not representable (the parent's world scale there is zero,
    // denormal enough to overflow the division, or the division otherwise yields inf/nan) keeps the
    // scale it already had rather than writing a value that would break every reader of this transform.
    if (const auto compensated = placement.scale[axis] / parentScale[axis]; std::isfinite(compensated))
    {
      localScale[axis] = compensated;
    }
  }

  transform->setPosition(placement.position - parentPosition);
  transform->setRotation(placement.rotation - parentRotation);
  transform->setScale(localScale);
}
