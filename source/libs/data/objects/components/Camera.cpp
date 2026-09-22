#include "Camera.h"
#include "FiniteCheck.h"
#include "WireTypes.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <Protocol.h>

Camera::Camera()
  : Component(ComponentType::camera)
{}

glm::vec3 Camera::getDirection() const
{
  return m_direction;
}

void Camera::setDirection(const glm::vec3& direction)
{
  if (!finiteCheck::isFinite(direction))
  {
    return;
  }

  m_direction = direction;
}

float Camera::getFov() const
{
  return m_fov;
}

void Camera::setFov(const float fov)
{
  // Before the clamp: nan compares false against both bounds, so std::clamp hands it straight back.
  if (!finiteCheck::isFinite(fov))
  {
    return;
  }

  m_fov = std::clamp(fov, minFovDegrees, maxFovDegrees);
}

float Camera::getNearPlane() const
{
  return m_nearPlane;
}

void Camera::setNearPlane(const float nearPlane)
{
  setNearFarPlanes(nearPlane, m_farPlane);
}

float Camera::getFarPlane() const
{
  return m_farPlane;
}

void Camera::setFarPlane(const float farPlane)
{
  setNearFarPlanes(m_nearPlane, farPlane);
}

float Camera::minFarPlaneFor(const float nearPlane)
{
  const float withClearance = nearPlane + minFarPlaneClearance;
  const float nextRepresentable = std::nextafter(nearPlane, std::numeric_limits<float>::infinity());

  // withClearance can round back down to nearPlane once float's ULP exceeds minFarPlaneClearance; taking
  // the max with the next representable float guarantees the result is still strictly greater than near.
  return std::max(withClearance, nextRepresentable);
}

void Camera::setNearFarPlanes(const float nearPlane, const float farPlane)
{
  const float newNear = finiteCheck::isFinite(nearPlane) ? std::max(nearPlane, minNearPlane) : m_nearPlane;
  const float newFar = finiteCheck::isFinite(farPlane) ? farPlane : m_farPlane;

  // far is always the one adjusted to satisfy the relation, never near, so a caller's near value is
  // never silently overridden by a stale far.
  m_nearPlane = newNear;
  m_farPlane = std::max(newFar, minFarPlaneFor(newNear));
}

bool Camera::isActive() const
{
  return m_active;
}

void Camera::setActive(const bool active)
{
  m_active = active;
}

nlohmann::json Camera::serialize()
{
  return {
    { "type", "Camera" },
    { "direction", { m_direction.x, m_direction.y, m_direction.z } },
    { "fov", m_fov },
    { "nearPlane", m_nearPlane },
    { "farPlane", m_farPlane },
    { "active", m_active }
  };
}

void Camera::loadFromJSON(const nlohmann::json& componentData)
{
  // value(...) so an older scene without a field defaults cleanly.
  if (const auto it = componentData.find("direction"); it != componentData.end() && it->size() == 3)
  {
    m_direction = glm::vec3(it->at(0), it->at(1), it->at(2));
  }

  setFov(componentData.value("fov", 45.0f));
  setNearFarPlanes(componentData.value("nearPlane", 0.1f), componentData.value("farPlane", 1000.0f));
  m_active = componentData.value("active", true);
}

void Camera::pack(net::Message& message) const
{
  message.write(ComponentType::camera);

  message.write(m_direction);
  message.write(m_fov);
  message.write(m_nearPlane);
  message.write(m_farPlane);
  message.write(m_active);
}

void Camera::unpack(net::MessageReader& messageReader)
{
  m_direction = messageReader.read<glm::vec3>();

  setFov(messageReader.read<float>());
  const float nearPlane = messageReader.read<float>();
  const float farPlane = messageReader.read<float>();
  setNearFarPlanes(nearPlane, farPlane);

  m_active = messageReader.read<bool>();
}
