#include "BoxCollider.h"
#include "../Transform.h"
#include "../../Object.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>
#include <limits>
#include <stdexcept>
#include <Protocol.h>

BoxCollider::BoxCollider()
  : Collider(ColliderType::boxCollider, ComponentType::SubComponentType_boxCollider)
{
  loadVariable(m_position);
  loadVariable(m_scale);
  loadVariable(m_rotation);
}

bool BoxCollider::getRenderCollider() const
{
  return m_renderCollider;
}

void BoxCollider::setRenderCollider(const bool renderCollider)
{
  m_renderCollider = renderCollider;
}

glm::vec3 BoxCollider::getLocalPosition() const
{
  return m_position.get();
}

glm::vec3 BoxCollider::getLocalScale() const
{
  return m_scale.get();
}

glm::vec3 BoxCollider::getLocalRotation() const
{
  return m_rotation.get();
}

void BoxCollider::setPosition(const glm::vec3& position)
{
  m_position.set(position);
  m_meshDirty = true;
}

void BoxCollider::setScale(const glm::vec3& scale)
{
  m_scale.set(scale);
  m_meshDirty = true;
}

void BoxCollider::setRotation(const glm::vec3& rotation)
{
  m_rotation.set(rotation);
  m_meshDirty = true;
}

nlohmann::json BoxCollider::serialize()
{
  const auto position = m_position.getInitialValue();
  const auto rotation = m_rotation.getInitialValue();
  const auto scale = m_scale.getInitialValue();

  const nlohmann::json data = {
    { "type", "Collider" },
    { "subType", "Box" },
    { "position", { position.x, position.y, position.z } },
    { "rotation", { rotation.x, rotation.y, rotation.z } },
    { "scale", { scale.x, scale.y, scale.z } },
    { "isTrigger", m_isTrigger }
  };

  return data;
}

void BoxCollider::loadFromJSON(const nlohmann::json& componentData)
{
  const auto& position = componentData.at("position");
  const auto& rotation = componentData.at("rotation");
  const auto& scale = componentData.at("scale");

  m_position.set(glm::vec3(position.at(0), position.at(1), position.at(2)));
  m_rotation.set(glm::vec3(rotation.at(0), rotation.at(1), rotation.at(2)));
  m_scale.set(glm::vec3(scale.at(0), scale.at(1), scale.at(2)));

  // value() (not at()): projects saved before triggers existed have no isTrigger key.
  m_isTrigger = componentData.value("isTrigger", false);

  m_meshDirty = true;
}

glm::vec3 BoxCollider::getPosition()
{
  updateTransformPointer();

  const std::shared_ptr<Transform> transform = m_transform_ptr.lock();

  return m_position.value() + transform->getPosition();
}

glm::vec3 BoxCollider::getScale()
{
  updateTransformPointer();

  const std::shared_ptr<Transform> transform = m_transform_ptr.lock();

  return m_scale.value() + transform->getScale();
}

glm::vec3 BoxCollider::getRotation()
{
  updateTransformPointer();

  const std::shared_ptr<Transform> transform = m_transform_ptr.lock();

  return m_rotation.value() + transform->getRotation();
}

glm::vec3 BoxCollider::findFurthestPoint(const glm::vec3& direction)
{
  updateTransformPointer();

  if (const std::shared_ptr<Transform> transform = m_transform_ptr.lock())
  {
    if (m_meshDirty || m_currentTransformUpdateID != transform->getUpdateID())
    {
      generateTransformedMesh(transform);
      m_meshDirty = false;
    }
  }

  float largestDot = std::numeric_limits<float>::lowest();
  glm::vec3 furthestVertex{ 0, 0, 0 };

  for (auto& vertex : m_transformedBoxVertices)
  {
    if (const float currentDot = dot(vertex, direction); currentDot > largestDot)
    {
      largestDot = currentDot;
      furthestVertex = vertex;
    }
  }

  return furthestVertex;
}

void BoxCollider::pack(net::Message& message) const
{
  message.write(ComponentType::SubComponentType_boxCollider);

  message.write(m_renderCollider);
  message.write(m_position.get());
  message.write(m_scale.get());
  message.write(m_rotation.get());
  message.write(m_isTrigger);
}

void BoxCollider::unpack(net::MessageReader& messageReader)
{
  m_renderCollider = messageReader.read<bool>();
  m_position.set(messageReader.read<glm::vec3>());
  m_scale.set(messageReader.read<glm::vec3>());
  m_rotation.set(messageReader.read<glm::vec3>());
  m_isTrigger = messageReader.read<bool>();
}

void BoxCollider::generateTransformedMesh(const std::shared_ptr<Transform>& transform)
{
  const auto rotation = transform->getRotation() + m_rotation.value();
  const auto scale = transform->getScale() * m_scale.value();
  const auto position = transform->getPosition() + m_position.value();

  const auto transformationMatrix = translate(glm::mat4(1.0f), position)
    * rotate(glm::mat4(1.0f), glm::radians(rotation.z), {0, 0, 1})
    * rotate(glm::mat4(1.0f), glm::radians(rotation.y), {0, 1, 0})
    * rotate(glm::mat4(1.0f), glm::radians(rotation.x), {1, 0, 0})
    * glm::scale(glm::mat4(1.0f), scale);

  for (size_t i = 0; i < boxVertices.size(); ++i)
  {
    const auto transformedVertex = transformationMatrix * glm::vec4(boxVertices[i], 1.0f);

    m_transformedBoxVertices[i] = glm::vec3(transformedVertex);
  }

  m_currentTransformUpdateID = transform->getUpdateID();
}

void BoxCollider::updateTransformPointer()
{
  if (m_transform_ptr.expired())
  {
    m_transform_ptr = m_owner->getComponent<Transform>(ComponentType::transform);

    if (m_transform_ptr.expired())
    {
      throw std::runtime_error("BoxCollider::updateTransformPointer::Missing transform component");
    }
  }
}
