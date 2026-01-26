#include "SphereCollider.h"
#include "../Transform.h"
#include "../../Object.h"
#include <glm/gtx/component_wise.inl>
#include <nlohmann/json.hpp>
#include <stdexcept>

SphereCollider::SphereCollider()
  : Collider(ColliderType::sphereCollider, ComponentType::SubComponentType_sphereCollider)
{}

float SphereCollider::getRadius()
{
  if (m_transform_ptr.expired())
  {
    m_transform_ptr = m_owner->getComponent<Transform>(ComponentType::transform);

    if (m_transform_ptr.expired())
    {
      throw std::runtime_error("MeshCollider::findFurthestPoint::Missing transform component");
    }
  }

  if (const std::shared_ptr<Transform> transform = m_transform_ptr.lock())
  {
    const float maxScale = compMax(transform->getScale());
    return m_radius * maxScale;
  }

  return 0;
}

void SphereCollider::displayGui()
{
  if (displayGuiHeader())
  {}
}

nlohmann::json SphereCollider::serialize()
{
  const nlohmann::json data = {
    { "type", "Collider" },
    { "subType", "Sphere" },
    { "radius", m_radius },
  };

  return data;
}

void SphereCollider::loadFromJSON(const nlohmann::json& componentData)
{
  m_radius = componentData["radius"];
}

glm::vec3 SphereCollider::findFurthestPoint(const glm::vec3& direction)
{
  if (m_transform_ptr.expired())
  {
    m_transform_ptr = m_owner->getComponent<Transform>(ComponentType::transform);

    if (m_transform_ptr.expired())
    {
      throw std::runtime_error("MeshCollider::findFurthestPoint::Missing transform component");
    }
  }

  if (const std::shared_ptr<Transform> transform = m_transform_ptr.lock())
  {
    return direction * m_radius * transform->getScale() + transform->getPosition();
  }

  return { 0, 0, 0 };
}
