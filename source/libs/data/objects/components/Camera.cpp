#include "Camera.h"
#include <nlohmann/json.hpp>
#include <Protocol.h>

Camera::Camera()
  : Component(ComponentType::camera)
{}

float Camera::getFov() const
{
  return m_fov;
}

void Camera::setFov(const float fov)
{
  m_fov = fov;
}

float Camera::getNearPlane() const
{
  return m_nearPlane;
}

void Camera::setNearPlane(const float nearPlane)
{
  m_nearPlane = nearPlane;
}

float Camera::getFarPlane() const
{
  return m_farPlane;
}

void Camera::setFarPlane(const float farPlane)
{
  m_farPlane = farPlane;
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
    { "fov", m_fov },
    { "nearPlane", m_nearPlane },
    { "farPlane", m_farPlane },
    { "active", m_active }
  };
}

void Camera::loadFromJSON(const nlohmann::json& componentData)
{
  // value(...) so an older scene without a field defaults cleanly.
  m_fov = componentData.value("fov", 45.0f);
  m_nearPlane = componentData.value("nearPlane", 0.1f);
  m_farPlane = componentData.value("farPlane", 1000.0f);
  m_active = componentData.value("active", true);
}

void Camera::pack(net::Message& message) const
{
  message.write(ComponentType::camera);

  message.write(m_fov);
  message.write(m_nearPlane);
  message.write(m_farPlane);
  message.write(m_active);
}

void Camera::unpack(net::MessageReader& messageReader)
{
  m_fov = messageReader.read<float>();
  m_nearPlane = messageReader.read<float>();
  m_farPlane = messageReader.read<float>();
  m_active = messageReader.read<bool>();
}
