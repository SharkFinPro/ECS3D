#include "GeometryKey.h"
#include <objects/Object.h>
#include <objects/components/Component.h>
#include <objects/components/Transform.h>

uint64_t geometryKeyOf(const Object& object)
{
  const auto transform = object.getComponent<Transform>(ComponentType::transform);
  if (!transform)
  {
    return 0;
  }

  uint64_t key = transform->getUpdateID();

  // Transform::getPosition stops at the first ancestor without a Transform, so the walk does too.
  auto current = object.getParent();
  while (current)
  {
    const auto parentTransform = current->getComponent<Transform>(ComponentType::transform);
    if (!parentTransform)
    {
      break;
    }

    key += parentTransform->getUpdateID();
    current = current->getParent();
  }

  return key;
}
