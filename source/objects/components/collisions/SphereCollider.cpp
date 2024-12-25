#include "SphereCollider.h"
#include "../Transform.h"
#include "../../Object.h"
#include <glm/gtx/component_wise.inl>
#include <imgui.h>
#include <stdexcept>

SphereCollider::SphereCollider()
  : Collider(ColliderType::sphereCollider, ComponentType::SubComponentType_sphereCollider)
{}

float SphereCollider::getRadius()
{
  if (transform_ptr.expired())
  {
    transform_ptr = std::dynamic_pointer_cast<Transform>(owner->getComponent(ComponentType::transform));

    if (transform_ptr.expired())
    {
      throw std::runtime_error("MeshCollider::findFurthestPoint::Missing transform component");
    }
  }

  if (const std::shared_ptr<Transform> transform = transform_ptr.lock())
  {
    const float maxScale = compMax(transform->getScale());
    return radius * maxScale;
  }

  return 0;
}

void SphereCollider::displayGui()
{
  if (displayGuiHeader())
  {}
}

glm::vec3 SphereCollider::findFurthestPoint(const glm::vec3& direction)
{
  if (transform_ptr.expired())
  {
    transform_ptr = std::dynamic_pointer_cast<Transform>(owner->getComponent(ComponentType::transform));

    if (transform_ptr.expired())
    {
      throw std::runtime_error("MeshCollider::findFurthestPoint::Missing transform component");
    }
  }

  if (const std::shared_ptr<Transform> transform = transform_ptr.lock())
  {
    return direction * radius * transform->getScale() + transform->getPosition();
  }

  return { 0, 0, 0 };
}
