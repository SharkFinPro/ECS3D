#include "Transform.h"
#include "../Object.h"
#include <nlohmann/json.hpp>
#include <Protocol.h>

Transform::Transform()
  : Component(ComponentType::transform)
{
  loadVariable(m_position);
  loadVariable(m_scale);
  loadVariable(m_rotation);
}

Transform::Transform(const glm::vec3& position, const glm::vec3& scale, const glm::vec3& rotation)
  : Component(ComponentType::transform),
    m_position(position),
    m_scale(scale),
    m_rotation(rotation)
{
  loadVariable(m_position);
  loadVariable(m_scale);
  loadVariable(m_rotation);
}

uint8_t Transform::getUpdateID() const
{
  return m_updateID;
}

glm::vec3 Transform::getPosition() const
{
  if (m_owner->getParent())
  {
    if (const auto& parentTransform = m_owner->getParent()->getComponent<Transform>(ComponentType::transform))
    {
      return parentTransform->getPosition() + m_position.get();
    }
  }

  return m_position.get();
}

glm::vec3 Transform::getScale() const
{
  if (m_owner->getParent())
  {
    if (const auto& parentTransform = m_owner->getParent()->getComponent<Transform>(ComponentType::transform))
    {
      return parentTransform->getScale() * m_scale.get();
    }
  }

  return m_scale.get();
}

glm::vec3 Transform::getRotation() const
{
  if (m_owner->getParent())
  {
    if (const auto& parentTransform = m_owner->getParent()->getComponent<Transform>(ComponentType::transform))
    {
      return parentTransform->getRotation() + m_rotation.get();
    }
  }

  return m_rotation.get();
}

glm::vec3 Transform::getLocalPosition() const
{
  return m_position.get();
}

glm::vec3 Transform::getLocalScale() const
{
  return m_scale.get();
}

glm::vec3 Transform::getLocalRotation() const
{
  return m_rotation.get();
}

void Transform::setPosition(const glm::vec3 position)
{
  m_position.set(position);
  ++m_updateID;
}

void Transform::setScale(const glm::vec3 scale)
{
  m_scale.set(scale);
  ++m_updateID;
}

void Transform::setRotation(const glm::vec3 rotation)
{
  m_rotation.set(rotation);
  ++m_updateID;
}

void Transform::move(const glm::vec3& direction)
{
  m_position.value() += direction;
  ++m_updateID;
}

nlohmann::json Transform::serialize()
{
  const auto position = m_position.getInitialValue();
  const auto rotation = m_rotation.getInitialValue();
  const auto scale = m_scale.getInitialValue();

  const nlohmann::json data = {
    { "type", "Transform" },
    { "position", { position.x, position.y, position.z } },
    { "rotation", { rotation.x, rotation.y, rotation.z } },
    { "scale", { scale.x, scale.y, scale.z } }
  };

  return data;
}

void Transform::loadFromJSON(const nlohmann::json& componentData)
{
  const auto& position = componentData.at("position");
  const auto& rotation = componentData.at("rotation");
  const auto& scale = componentData.at("scale");

  m_position.set(glm::vec3(position.at(0), position.at(1), position.at(2)));
  m_rotation.set(glm::vec3(rotation.at(0), rotation.at(1), rotation.at(2)));
  m_scale.set(glm::vec3(scale.at(0), scale.at(1), scale.at(2)));
}

void Transform::pack(net::Message& message) const
{
  message.write(ComponentType::transform);

  message.write(m_position.get());
  message.write(m_scale.get());
  message.write(m_rotation.get());
}

void Transform::unpack(net::MessageReader& messageReader)
{
  m_position.set(messageReader.read<glm::vec3>());
  m_scale.set(messageReader.read<glm::vec3>());
  m_rotation.set(messageReader.read<glm::vec3>());
}
