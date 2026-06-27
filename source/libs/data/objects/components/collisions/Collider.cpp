#include "Collider.h"
#include "../Transform.h"
#include "../../Object.h"
#include <stdexcept>

Collider::Collider(const ColliderType type, const ComponentType subType)
  : Component(ComponentType::collider, subType), m_colliderType(type)
{}

const BoundingBox& Collider::getBoundingBox()
{
  if (m_transform_ptr.expired())
  {
    m_transform_ptr = m_owner->getComponent<Transform>(ComponentType::transform);

    if (m_transform_ptr.expired())
    {
      throw std::runtime_error("Collider::getBoundingBox::Missing Transform");
    }
  }

  const std::shared_ptr<Transform> transform = m_transform_ptr.lock();
  const uint8_t transformUpdateID = transform->getUpdateID();

  if (m_boundingBox.lastUpdateID == transformUpdateID)
  {
    return m_boundingBox;
  }

  m_boundingBox.lastUpdateID = transformUpdateID;

  m_boundingBox.minX = findFurthestPoint({-1, 0, 0}).x;
  m_boundingBox.maxX = findFurthestPoint({1, 0, 0}).x;

  m_boundingBox.minY = findFurthestPoint({0, -1, 0}).y;
  m_boundingBox.maxY = findFurthestPoint({0, 1, 0}).y;

  m_boundingBox.minZ = findFurthestPoint({0, 0, -1}).z;
  m_boundingBox.maxZ = findFurthestPoint({0, 0, 1}).z;

  return m_boundingBox;
}

ColliderType Collider::getColliderType() const
{
  return m_colliderType;
}
