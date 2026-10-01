#include "Transform.h"
#include "FiniteCheck.h"
#include "RotationConvention.h"
#include "../Object.h"
#include "WireTypes.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <Protocol.h>
#include <algorithm>
#include <atomic>

namespace {
  // Process-wide so a stamp is never reused by another transform: a world cache keyed on the largest stamp
  // in a chain then still moves when an object is reparented under a chain with smaller stamps.
  std::atomic<uint64_t> g_stampClock{0};

  uint64_t nextStamp()
  {
    return ++g_stampClock;
  }

  std::shared_ptr<Transform> parentTransformOf(const Object& owner)
  {
    if (const auto& parent = owner.getParent())
    {
      return parent->getComponent<Transform>(ComponentType::transform);
    }

    return nullptr;
  }

  // Drops a non-finite value, keeping the previous one.
  void setFiniteLocal(ComponentVariable<glm::vec3>& variable, const glm::vec3& value)
  {
    if (finiteCheck::isFinite(value))
    {
      variable.set(value);
    }
  }
}

Transform::Transform()
  : Transform(glm::vec3(0), glm::vec3(1), glm::vec3(0))
{}

Transform::Transform(const glm::vec3& position, const glm::vec3& scale, const glm::vec3& rotation)
  : Component(ComponentType::transform),
    m_position(position),
    m_scale(scale),
    m_rotation(rotation),
    m_worldStamp(nextStamp())
{
  loadVariable(m_position);
  loadVariable(m_scale);
  loadVariable(m_rotation);
}

bool Transform::hasParentTransform() const
{
  return parentTransformOf(*m_owner) != nullptr;
}

glm::vec3 Transform::getPosition() const
{
  if (const auto parentTransform = parentTransformOf(*m_owner))
  {
    return parentTransform->getPosition()
      + parentTransform->getOrientation() * (parentTransform->getScale() * m_position.get());
  }

  return m_position.get();
}

// Component-wise: a non-uniform parent scale over a rotated child would shear, which a scale vector cannot hold.
glm::vec3 Transform::getScale() const
{
  if (const auto parentTransform = parentTransformOf(*m_owner))
  {
    return parentTransform->getScale() * m_scale.get();
  }

  return m_scale.get();
}

glm::quat Transform::getOrientation() const
{
  if (const auto parentTransform = parentTransformOf(*m_owner))
  {
    return parentTransform->getOrientation() * eulerDegreesToQuat(m_rotation.get());
  }

  return eulerDegreesToQuat(m_rotation.get());
}

glm::vec3 Transform::getRotation() const
{
  if (parentTransformOf(*m_owner))
  {
    return quatToEulerDegrees(getOrientation());
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
  if (!finiteCheck::isFinite(position))
  {
    return;
  }

  m_position.set(position);
  touch();
}

void Transform::setScale(const glm::vec3 scale)
{
  if (!finiteCheck::isFinite(scale))
  {
    return;
  }

  m_scale.set(scale);
  touch();
}

void Transform::setRotation(const glm::vec3 rotation)
{
  if (!finiteCheck::isFinite(rotation))
  {
    return;
  }

  m_rotation.set(rotation);
  touch();
}

void Transform::setWorldRotation(const glm::vec3 rotation)
{
  if (parentTransformOf(*m_owner))
  {
    setWorldOrientation(eulerDegreesToQuat(rotation));
    return;
  }

  setRotation(rotation);
}

void Transform::setWorldOrientation(const glm::quat& orientation)
{
  if (const auto parentTransform = parentTransformOf(*m_owner))
  {
    setRotation(quatToEulerDegrees(glm::inverse(parentTransform->getOrientation()) * orientation));
    return;
  }

  setRotation(quatToEulerDegrees(orientation));
}

void Transform::start()
{
  Component::start();

  // Reseeds the live values from initial without going through a setter - bump here so a cached mesh or
  // bounding box keyed on the world stamp rebuilds against the reseeded transform on the first tick.
  touch();
}

void Transform::stop()
{
  Component::stop();

  // Same reseed, the other direction: live reverts to initial on stop.
  touch();
}

void Transform::move(const glm::vec3& direction)
{
  // Scripts reach this through the Move binding, so it needs the same guard the setters have - and the
  // sum is checked rather than the direction, since a finite step off an already huge position
  // overflows to infinity on its own.
  const auto moved = m_position.get() + direction;

  if (!finiteCheck::isFinite(moved))
  {
    return;
  }

  m_position.set(moved);
  touch();
}

void Transform::moveWorld(const glm::vec3& displacement)
{
  const auto parentTransform = parentTransformOf(*m_owner);
  if (!parentTransform)
  {
    move(displacement);
    return;
  }

  const auto inParentFrame = glm::inverse(parentTransform->getOrientation()) * displacement;
  const auto parentScale = parentTransform->getScale();

  glm::vec3 local = inParentFrame;
  for (int axis = 0; axis < 3; ++axis)
  {
    if (const auto compensated = inParentFrame[axis] / parentScale[axis]; std::isfinite(compensated))
    {
      local[axis] = compensated;
    }
  }

  move(local);
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

  setFiniteLocal(m_position, finiteCheck::readVec3OrNaN(position));
  setFiniteLocal(m_rotation, finiteCheck::readVec3OrNaN(rotation));
  setFiniteLocal(m_scale, finiteCheck::readVec3OrNaN(scale));

  // Bypasses the setters, so bump directly - a collider cache keyed on the world stamp has to know this
  // geometry changed.
  touch();
}

void Transform::pack(net::Message& message) const
{
  message.write(ComponentType::transform);

  message.write(m_position.get());
  message.write(m_rotation.get());
  message.write(m_scale.get());
}

void Transform::unpack(net::MessageReader& messageReader)
{
  setFiniteLocal(m_position, messageReader.read<glm::vec3>());
  setFiniteLocal(m_rotation, messageReader.read<glm::vec3>());
  setFiniteLocal(m_scale, messageReader.read<glm::vec3>());

  // Bypasses the setters, so bump directly - see loadFromJSON.
  touch();
}

uint64_t Transform::getWorldUpdateID() const
{
  uint64_t worldID = m_worldStamp;

  // Stops where getPosition/getScale/getRotation stop. Reads the component map directly to skip a refcount
  // round trip per level, since this runs inside the collision support function.
  auto ancestor = m_owner->getParent();

  while (ancestor)
  {
    const auto& components = ancestor->getComponents();
    const auto transformIt = components.find(ComponentType::transform);

    if (transformIt == components.end())
    {
      break;
    }

    worldID = std::max(worldID, static_cast<const Transform*>(transformIt->second.get())->m_worldStamp);
    ancestor = ancestor->getParent();
  }

  return worldID;
}

void Transform::markReparented()
{
  m_worldStamp = nextStamp();
}

void Transform::touch()
{
  m_worldStamp = nextStamp();
}
