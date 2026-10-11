#include "CapsuleCollider.h"
#include "../FiniteCheck.h"
#include "../RotationConvention.h"
#include "../Transform.h"
#include "../../Object.h"
#include "WireTypes.h"
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <Protocol.h>

CapsuleCollider::CapsuleCollider()
  : Collider(ColliderType::capsuleCollider, ComponentType::SubComponentType_capsuleCollider)
{
  loadVariable(m_position);
  loadVariable(m_rotation);
  loadVariable(m_radius);
  loadVariable(m_height);
}

glm::vec3 CapsuleCollider::getLocalPosition() const
{
  return m_position.get();
}

glm::vec3 CapsuleCollider::getLocalRotation() const
{
  return m_rotation.get();
}

float CapsuleCollider::getLocalRadius() const
{
  return m_radius.get();
}

float CapsuleCollider::getLocalHeight() const
{
  return m_height.get();
}

void CapsuleCollider::setPosition(const glm::vec3& position)
{
  if (!finiteCheck::isFinite(position))
  {
    return;
  }

  m_position.set(position);
  invalidateBoundingBox();
}

void CapsuleCollider::setRotation(const glm::vec3& rotation)
{
  if (!finiteCheck::isFinite(rotation))
  {
    return;
  }

  m_rotation.set(rotation);
  invalidateBoundingBox();
}

void CapsuleCollider::setRadius(const float radius)
{
  if (!finiteCheck::isFinite(radius))
  {
    return;
  }

  m_radius.set(std::max(radius, 0.0f));
  invalidateBoundingBox();
}

void CapsuleCollider::setHeight(const float height)
{
  if (!finiteCheck::isFinite(height))
  {
    return;
  }

  m_height.set(std::max(height, 0.0f));
  invalidateBoundingBox();
}

float CapsuleCollider::getRadius()
{
  updateTransformPointer();

  return getScaledRadius(m_transform_ptr.lock());
}

CapsuleSegment CapsuleCollider::getSegment()
{
  updateTransformPointer();

  const std::shared_ptr<Transform> transform = m_transform_ptr.lock();

  const glm::vec3 center = transform->getPosition() + m_position.get();
  const glm::vec3 offset = getWorldAxis(transform) * getSegmentHalfLength(transform);

  return { center - offset, center + offset };
}

nlohmann::json CapsuleCollider::serialize()
{
  const auto position = m_position.getInitialValue();
  const auto rotation = m_rotation.getInitialValue();

  const nlohmann::json data = {
    { "type", "Collider" },
    { "subType", "Capsule" },
    { "position", { position.x, position.y, position.z } },
    { "rotation", { rotation.x, rotation.y, rotation.z } },
    { "radius", m_radius.getInitialValue() },
    { "height", m_height.getInitialValue() },
    { "renderCollider", m_renderCollider },
    { "isTrigger", m_isTrigger },
    { "layer", m_layer },
    { "mask", m_mask }
  };

  return data;
}

void CapsuleCollider::loadFromJSON(const nlohmann::json& componentData)
{
  const auto& position = componentData.at("position");
  const auto& rotation = componentData.at("rotation");

  m_position.set(glm::vec3(position.at(0), position.at(1), position.at(2)));
  m_rotation.set(glm::vec3(rotation.at(0), rotation.at(1), rotation.at(2)));
  m_radius.set(componentData.at("radius"));
  m_height.set(componentData.at("height"));

  // value() (not at()): a hand-authored blob may leave the flags out, and at() would throw the project away.
  m_renderCollider = componentData.value("renderCollider", false);
  m_isTrigger = componentData.value("isTrigger", false);
  setLayer(componentData.value("layer", 0u));
  m_mask = componentData.value("mask", 0xFFFFFFFFu);

  invalidateBoundingBox();
}

glm::vec3 CapsuleCollider::getPosition()
{
  updateTransformPointer();

  return m_position.get() + m_transform_ptr.lock()->getPosition();
}

glm::vec3 CapsuleCollider::getRotation()
{
  updateTransformPointer();

  const std::shared_ptr<Transform> transform = m_transform_ptr.lock();

  if (!transform->hasParentTransform())
  {
    return m_rotation.get() + transform->getRotation();
  }

  return quatToEulerDegrees(transform->getOrientation() * eulerDegreesToQuat(m_rotation.get()));
}

glm::vec3 CapsuleCollider::findFurthestPoint(const glm::vec3& direction)
{
  const auto [start, end] = getSegment();

  if (const float length = glm::length(direction); length > 0.0f)
  {
    const glm::vec3& endpoint = glm::dot(end, direction) > glm::dot(start, direction) ? end : start;

    return endpoint + direction / length * getRadius();
  }

  return (start + end) * 0.5f;
}

void CapsuleCollider::pack(net::Message& message) const
{
  message.write(ComponentType::SubComponentType_capsuleCollider);

  message.write(m_renderCollider);
  message.write(m_position.get());
  message.write(m_rotation.get());
  message.write(m_radius.get());
  message.write(m_height.get());
  message.write(m_isTrigger);
  message.write(m_layer);
  message.write(m_mask);
}

void CapsuleCollider::unpack(net::MessageReader& messageReader)
{
  m_renderCollider = messageReader.read<bool>();
  m_position.set(messageReader.read<glm::vec3>());
  m_rotation.set(messageReader.read<glm::vec3>());
  m_radius.set(messageReader.read<float>());
  m_height.set(messageReader.read<float>());
  m_isTrigger = messageReader.read<bool>();
  setLayer(messageReader.read<uint32_t>());
  m_mask = messageReader.read<uint32_t>();

  invalidateBoundingBox();
}

void CapsuleCollider::updateTransformPointer()
{
  if (m_transform_ptr.expired())
  {
    m_transform_ptr = m_owner->getComponent<Transform>(ComponentType::transform);

    if (m_transform_ptr.expired())
    {
      throw std::runtime_error("CapsuleCollider::updateTransformPointer::Missing transform component");
    }
  }
}

float CapsuleCollider::getScaledRadius(const std::shared_ptr<Transform>& transform) const
{
  const glm::vec3 scale = glm::abs(transform->getScale());

  return m_radius.get() * std::max(scale.x, scale.z);
}

float CapsuleCollider::getSegmentHalfLength(const std::shared_ptr<Transform>& transform) const
{
  const float halfHeight = m_height.get() * 0.5f * std::abs(transform->getScale().y);

  return std::max(halfHeight - getScaledRadius(transform), 0.0f);
}

glm::vec3 CapsuleCollider::getWorldAxis(const std::shared_ptr<Transform>& transform) const
{
  // Same orientation BoxCollider builds: additive Euler without a parent, composed quaternions with one.
  const glm::quat orientation = transform->hasParentTransform()
    ? transform->getOrientation() * eulerDegreesToQuat(m_rotation.get())
    : eulerDegreesToQuat(transform->getRotation() + m_rotation.get());

  return orientation * glm::vec3(0.0f, 1.0f, 0.0f);
}
