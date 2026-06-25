#include "SphereCollider.h"
#include "../Transform.h"
#include "../../Object.h"
#include <glm/gtx/component_wise.inl>
#include <nlohmann/json.hpp>
#include <stdexcept>

SphereCollider::SphereCollider()
  : Collider(ColliderType::sphereCollider, ComponentType::SubComponentType_sphereCollider)
{
  loadVariable(m_radius);
  loadVariable(m_position);
}

float SphereCollider::getRadius()
{
  updateTransformPointer();

  if (const std::shared_ptr<Transform> transform = m_transform_ptr.lock())
  {
    return getScaledRadius(transform);
  }

  return 0;
}

float SphereCollider::getLocalRadius() const
{
  return m_radius.get();
}

void SphereCollider::setRadius(const float radius)
{
  m_radius.set(radius);
}

glm::vec3 SphereCollider::getLocalPosition() const
{
  return m_position.get();
}

void SphereCollider::setPosition(const glm::vec3& position)
{
  m_position.set(position);
}

bool SphereCollider::getRenderCollider() const
{
  return m_renderCollider;
}

void SphereCollider::setRenderCollider(const bool renderCollider)
{
  m_renderCollider = renderCollider;
}

nlohmann::json SphereCollider::serialize()
{
  const auto position = m_position.getInitialValue();

  const nlohmann::json data = {
    { "type", "Collider" },
    { "subType", "Sphere" },
    { "radius", m_radius.getInitialValue() },
    { "renderCollider", m_renderCollider },
    { "position", { position.x, position.y, position.z } }
  };

  return data;
}

void SphereCollider::loadFromJSON(const nlohmann::json& componentData)
{
  const auto& position = componentData.at("position");
  m_position.set(glm::vec3(position.at(0), position.at(1), position.at(2)));

  m_radius.set(componentData.at("radius"));
  m_renderCollider = componentData.at("renderCollider");
}

glm::vec3 SphereCollider::getPosition()
{
  updateTransformPointer();

  const std::shared_ptr<Transform> transform = m_transform_ptr.lock();

  return m_position.value() + transform->getPosition();
}

glm::vec3 SphereCollider::findFurthestPoint(const glm::vec3& direction)
{
  updateTransformPointer();

  if (const std::shared_ptr<Transform> transform = m_transform_ptr.lock())
  {
    return direction * getScaledRadius(transform) + transform->getPosition() + m_position.value();
  }

  return { 0, 0, 0 };
}

void SphereCollider::updateTransformPointer()
{
  if (m_transform_ptr.expired())
  {
    m_transform_ptr = m_owner->getComponent<Transform>(ComponentType::transform);

    if (m_transform_ptr.expired())
    {
      throw std::runtime_error("SphereCollider::updateTransformPointer::Missing transform component");
    }
  }
}

float SphereCollider::getScaledRadius(const std::shared_ptr<Transform>& transform)
{
  const auto maxScale = compMax(transform->getScale());

  return maxScale * m_radius.value();
}
