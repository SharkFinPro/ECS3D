#include "Transform.h"
#include "RigidBody.h"
#include "../Object.h"
#include "../../GuiComponents.h"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <memory>

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

void Transform::displayGui()
{
  if (displayGuiHeader())
  {
    ImGui::PushID(1);
    gc::xyzGui("Position", &m_position.value().x, &m_position.value().y, &m_position.value().z);
    ImGui::PopID();

    ImGui::PushID(2);
    gc::xyzGui("Rotation", &m_rotation.value().x, &m_rotation.value().y, &m_rotation.value().z);
    ImGui::PopID();

    ImGui::PushID(3);
    gc::xyzGui("Scale", &m_scale.value().x, &m_scale.value().y, &m_scale.value().z);

    float combinedScale = (m_scale.value().x + m_scale.value().y + m_scale.value().z) / 3.0f;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Scale All");
    ImGui::SameLine(110.0f);
    if (ImGui::DragFloat("##Scale All", &combinedScale, 0.1f))
    {
      m_scale.value().x = m_scale.value().y = m_scale.value().z = combinedScale;
    }

    ImGui::PopID();

    ++m_updateID;
  }
}

nlohmann::json Transform::serialize()
{
  const auto position = m_position.value();
  const auto rotation = m_rotation.value();
  const auto scale = m_scale.value();

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
