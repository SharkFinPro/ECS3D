#include "Collider.h"
#include "../Transform.h"
#include "../../Object.h"
#include <cassert>
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
  const uint64_t transformUpdateID = transform->getUpdateID();

  if (m_boundingBox.lastUpdateID == transformUpdateID && !m_boundingBoxDirty)
  {
    return m_boundingBox;
  }

  computeBounds(m_boundingBox);

  m_boundingBox.lastUpdateID = transformUpdateID;
  m_boundingBoxDirty = false;

  return m_boundingBox;
}

void Collider::computeBounds(BoundingBox& box)
{
  box.minX = findFurthestPoint({-1, 0, 0}).x;
  box.maxX = findFurthestPoint({1, 0, 0}).x;

  box.minY = findFurthestPoint({0, -1, 0}).y;
  box.maxY = findFurthestPoint({0, 1, 0}).y;

  box.minZ = findFurthestPoint({0, 0, -1}).z;
  box.maxZ = findFurthestPoint({0, 0, 1}).z;
}

const BoundingBox& Collider::cachedBoundingBox() const
{
  // Fails loudly in a debug build if a caller reaches this before getBoundingBox() has ever warmed the
  // cache - the precondition documented on the declaration, not something this accessor can enforce by
  // recomputing without breaking the whole reason it exists.
  assert(!m_boundingBoxDirty);

  return m_boundingBox;
}

ColliderType Collider::getColliderType() const
{
  return m_colliderType;
}

bool Collider::isTrigger() const
{
  return m_isTrigger;
}

void Collider::setIsTrigger(const bool isTrigger)
{
  m_isTrigger = isTrigger;
}

uint32_t Collider::getLayer() const
{
  return m_layer;
}

void Collider::setLayer(const uint32_t layer)
{
  // Only layers 0-31 exist (mask is 32 bits); clamp so a stray value can't shift out of range.
  m_layer = layer > 31u ? 31u : layer;
}

uint32_t Collider::getMask() const
{
  return m_mask;
}

void Collider::setMask(const uint32_t mask)
{
  m_mask = mask;
}

bool Collider::getRenderCollider() const
{
  return m_renderCollider;
}

void Collider::setRenderCollider(const bool renderCollider)
{
  m_renderCollider = renderCollider;
}

void Collider::invalidateBoundingBox()
{
  m_boundingBoxDirty = true;
}
