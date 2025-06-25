#include "Transform.h"
#include "RigidBody.h"
#include "../Object.h"
#include <imgui.h>
#include <memory>

Transform::Transform(const glm::vec3& position, const glm::vec3& scale, const glm::vec3& rotation)
  : Component(ComponentType::transform),
    updateID(1),
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
  return updateID;
}

glm::vec3 Transform::getPosition() const
{
  if (owner->getParent())
  {
    if (const auto& parentTransform = owner->getParent()->getComponent<Transform>(ComponentType::transform))
    {
      return parentTransform->getPosition() + m_position.get();
    }
  }

  return m_position.get();
}

glm::vec3 Transform::getScale() const
{
  if (owner->getParent())
  {
    if (const auto& parentTransform = owner->getParent()->getComponent<Transform>(ComponentType::transform))
    {
      return parentTransform->getScale() * m_scale.get();
    }
  }

  return m_scale.get();
}

glm::vec3 Transform::getRotation() const
{
  if (owner->getParent())
  {
    if (const auto& parentTransform = owner->getParent()->getComponent<Transform>(ComponentType::transform))
    {
      return parentTransform->getRotation() + m_rotation.get();
    }
  }

  return m_rotation.get();
}

void Transform::setScale(const glm::vec3 scale)
{
  m_scale.set(scale);
  ++updateID;
}

void Transform::setRotation(const glm::vec3 rotation)
{
  m_rotation.set(rotation);
  ++updateID;
}

void Transform::move(const glm::vec3& direction)
{
  m_position.value() += direction;
  ++updateID;
}

void Transform::displayGui()
{
  if (displayGuiHeader())
  {
    ImGui::PushID(1);
    ImGui::Text("Control Position:");
    ImGui::SliderFloat("x", &m_position.value().x, -30.0f, 30.0f);
    ImGui::SliderFloat("y", &m_position.value().y, -30.0f, 30.0f);
    ImGui::SliderFloat("z", &m_position.value().z, -30.0f, 30.0f);
    ImGui::PopID();

    ImGui::PushID(2);
    ImGui::Text("Control Rotation:");
    ImGui::SliderFloat("x", &m_rotation.value().x, 0.0f, 360.0f);
    ImGui::SliderFloat("y", &m_rotation.value().y, 0.0f, 360.0f);
    ImGui::SliderFloat("z", &m_rotation.value().z, 0.0f, 360.0f);
    ImGui::PopID();

    ImGui::PushID(3);
    ImGui::Text("Control Scale:");
    ImGui::SliderFloat("x", &m_scale.value().x, 0.1f, 10.0f);
    ImGui::SliderFloat("y", &m_scale.value().y, 0.1f, 10.0f);
    ImGui::SliderFloat("z", &m_scale.value().z, 0.1f, 10.0f);

    float combinedScale = (m_scale.value().x + m_scale.value().y + m_scale.value().z) / 3.0f;
    if (ImGui::SliderFloat("xyz", &combinedScale, 0.1f, 10.0f))
    {
      m_scale.value().x = m_scale.value().y = m_scale.value().z = combinedScale;
    }

    ++updateID;

    ImGui::PopID();
  }
}
