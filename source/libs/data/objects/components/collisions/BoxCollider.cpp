#include "BoxCollider.h"
#include "../FiniteCheck.h"
#include "../RotationConvention.h"
#include "../Transform.h"
#include "../../Object.h"
#include "WireTypes.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <stdexcept>
#include <Protocol.h>

BoxCollider::BoxCollider()
  : Collider(ColliderType::boxCollider, ComponentType::SubComponentType_boxCollider)
{
  loadVariable(m_position);
  loadVariable(m_scale);
  loadVariable(m_rotation);
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
  if (!finiteCheck::isFinite(position))
  {
    return;
  }

  m_position.set(position);
  m_meshDirty = true;
  invalidateBoundingBox();
}

void BoxCollider::setScale(const glm::vec3& scale)
{
  if (!finiteCheck::isFinite(scale))
  {
    return;
  }

  m_scale.set(scale);
  m_meshDirty = true;
  invalidateBoundingBox();
}

void BoxCollider::setRotation(const glm::vec3& rotation)
{
  if (!finiteCheck::isFinite(rotation))
  {
    return;
  }

  m_rotation.set(rotation);
  m_meshDirty = true;
  invalidateBoundingBox();
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
    { "renderCollider", m_renderCollider },
    { "isTrigger", m_isTrigger },
    { "layer", m_layer },
    { "mask", m_mask }
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

  // value() (not at()): projects saved before triggers/layers/the gizmo flag existed have no such key.
  m_renderCollider = componentData.value("renderCollider", false);
  m_isTrigger = componentData.value("isTrigger", false);
  setLayer(componentData.value("layer", 0u));
  m_mask = componentData.value("mask", 0xFFFFFFFFu);

  m_meshDirty = true;
  invalidateBoundingBox();
}

glm::vec3 BoxCollider::getPosition()
{
  updateTransformPointer();

  const std::shared_ptr<Transform> transform = m_transform_ptr.lock();

  return m_position.get() + transform->getPosition();
}

glm::vec3 BoxCollider::getScale()
{
  updateTransformPointer();

  const std::shared_ptr<Transform> transform = m_transform_ptr.lock();

  // Multiplied, not added: the collision mesh generateTransformedMesh builds scales the unit box by the
  // product, so an offset of 1 has to mean "same size as the object" rather than "one bigger".
  return m_scale.get() * transform->getScale();
}

glm::vec3 BoxCollider::getRotation()
{
  updateTransformPointer();

  const std::shared_ptr<Transform> transform = m_transform_ptr.lock();

  if (!transform->hasParentTransform())
  {
    return m_rotation.get() + transform->getRotation();
  }

  return quatToEulerDegrees(transform->getOrientation() * eulerDegreesToQuat(m_rotation.get()));
}

namespace
{
  // Maps a sign mask (bit 0 = +x, bit 1 = +y, bit 2 = +z) to the boxVertices index with those signs.
  constexpr std::array<uint8_t, 8> buildVertexBySignMask()
  {
    std::array<uint8_t, 8> table{};
    for (uint8_t i = 0; i < boxVertices.size(); ++i)
    {
      const auto& v = boxVertices[i];
      const int mask = (v.x > 0 ? 1 : 0) | (v.y > 0 ? 2 : 0) | (v.z > 0 ? 4 : 0);
      table[mask] = i;
    }
    return table;
  }

  constexpr std::array<uint8_t, 8> vertexBySignMask = buildVertexBySignMask();
}

glm::vec3 BoxCollider::findFurthestPoint(const glm::vec3& direction)
{
  if (const Transform& transform = meshTransform();
      m_meshDirty || m_currentTransformUpdateID != transform.getWorldUpdateID())
  {
    generateTransformedMesh(transform);
    m_meshDirty = false;
  }

  // dot(direction, vertex) is affine in the per-axis signs, so the furthest vertex takes the sign of each
  // axis' dot: 3 dots instead of scanning all 8 vertices.
  const int mask = (dot(direction, m_transformedBoxAxes[0]) >= 0.0f ? 1 : 0)
    | (dot(direction, m_transformedBoxAxes[1]) >= 0.0f ? 2 : 0)
    | (dot(direction, m_transformedBoxAxes[2]) >= 0.0f ? 4 : 0);

  return m_transformedBoxVertices[vertexBySignMask[mask]];
}

Transform& BoxCollider::meshTransform()
{
  updateTransformPointer();

  // owner_before compares control blocks without touching the reference counts lock() would bump.
  if (m_meshTransform == nullptr
      || m_meshTransformSource.owner_before(m_transform_ptr)
      || m_transform_ptr.owner_before(m_meshTransformSource))
  {
    m_meshTransform = m_transform_ptr.lock().get();
    m_meshTransformSource = m_transform_ptr;
  }

  return *m_meshTransform;
}

void BoxCollider::pack(net::Message& message) const
{
  message.write(ComponentType::SubComponentType_boxCollider);

  message.write(m_renderCollider);
  message.write(m_position.get());
  message.write(m_scale.get());
  message.write(m_rotation.get());
  message.write(m_isTrigger);
  message.write(m_layer);
  message.write(m_mask);
}

void BoxCollider::unpack(net::MessageReader& messageReader)
{
  m_renderCollider = messageReader.read<bool>();
  m_position.set(messageReader.read<glm::vec3>());
  m_scale.set(messageReader.read<glm::vec3>());
  m_rotation.set(messageReader.read<glm::vec3>());
  m_isTrigger = messageReader.read<bool>();
  setLayer(messageReader.read<uint32_t>());
  m_mask = messageReader.read<uint32_t>();

  m_meshDirty = true;
  invalidateBoundingBox();
}

void BoxCollider::generateTransformedMesh(const Transform& transform)
{
  const auto scale = transform.getScale() * m_scale.get();
  const auto position = transform.getPosition() + m_position.get();

  glm::mat4 rotationMatrix;
  if (!transform.hasParentTransform())
  {
    // Built exactly as getRotation's additive sum describes it, which the narrow phase and queries rebuild.
    const auto rotation = transform.getRotation() + m_rotation.get();
    rotationMatrix = rotate(glm::mat4(1.0f), glm::radians(rotation.z), {0, 0, 1})
      * rotate(glm::mat4(1.0f), glm::radians(rotation.y), {0, 1, 0})
      * rotate(glm::mat4(1.0f), glm::radians(rotation.x), {1, 0, 0});
  }
  else
  {
    rotationMatrix = glm::mat4_cast(transform.getOrientation() * eulerDegreesToQuat(m_rotation.get()));
  }

  const auto transformationMatrix = translate(glm::mat4(1.0f), position)
    * rotationMatrix
    * glm::scale(glm::mat4(1.0f), scale);

  for (size_t i = 0; i < boxVertices.size(); ++i)
  {
    const auto transformedVertex = transformationMatrix * glm::vec4(boxVertices[i], 1.0f);

    m_transformedBoxVertices[i] = glm::vec3(transformedVertex);
  }

  // Each pair below differs only in the one axis's sign, so the difference is that axis' world-space edge.
  m_transformedBoxAxes[0] = m_transformedBoxVertices[0] - m_transformedBoxVertices[1];
  m_transformedBoxAxes[1] = m_transformedBoxVertices[0] - m_transformedBoxVertices[4];
  m_transformedBoxAxes[2] = m_transformedBoxVertices[0] - m_transformedBoxVertices[3];

  m_currentTransformUpdateID = transform.getWorldUpdateID();
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
