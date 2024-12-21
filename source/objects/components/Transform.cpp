#include "Transform.h"
#include <imgui.h>
#include <memory>
#include "../Object.h"
#include "RigidBody.h"

Transform::Transform(const glm::vec3& position, const glm::vec3& scale, const glm::vec3& rotation)
  : Component(ComponentType::transform), initialPosition(position), position(initialPosition),
    initialScale(scale), scale(initialScale), initialRotation(rotation), rotation(initialRotation)
{}

glm::vec3 Transform::getPosition() const
{
  return position;
}

glm::vec3 Transform::getScale() const
{
  return scale;
}

glm::vec3 Transform::getRotation() const
{
  return rotation;
}

void Transform::setScale(const glm::vec3 scale)
{
  this->scale = scale;
}

void Transform::setRotation(const glm::vec3 rotation)
{
  this->rotation = rotation;
}

void Transform::move(const glm::vec3& direction)
{
  position += direction;
}

void Transform::reset()
{
  position = initialPosition;
  scale = initialScale;
  rotation = initialRotation;
}

void Transform::displayGui()
{
  if (ImGui::CollapsingHeader("Transform"))
  {
    ImGui::PushID(1);
    ImGui::Text("Control Position:");
    ImGui::SliderFloat("x", &position.x, -30.0f, 30.0f);
    ImGui::SliderFloat("y", &position.y, -30.0f, 30.0f);
    ImGui::SliderFloat("z", &position.z, -30.0f, 30.0f);
    ImGui::PopID();

    ImGui::PushID(2);
    ImGui::Text("Control Rotation:");
    ImGui::SliderFloat("x", &rotation.x, 0.0f, 360.0f);
    ImGui::SliderFloat("y", &rotation.y, 0.0f, 360.0f);
    ImGui::SliderFloat("z", &rotation.z, 0.0f, 360.0f);
    ImGui::PopID();

    ImGui::PushID(3);
    ImGui::Text("Control Scale:");
    ImGui::SliderFloat("x", &scale.x, 0.1f, 10.0f);
    ImGui::SliderFloat("y", &scale.y, 0.1f, 10.0f);
    ImGui::SliderFloat("z", &scale.z, 0.1f, 10.0f);

    float combinedScale = (scale.x + scale.y + scale.z) / 3.0f;
    if (ImGui::SliderFloat("Combined", &combinedScale, 0.1f, 10.0f))
    {
      scale.x = scale.y = scale.z = combinedScale;
    }

    ImGui::PopID();

    if (ImGui::Button("Reset"))
    {
      reset();

      if (const auto rigidBody = std::dynamic_pointer_cast<RigidBody>(owner->getComponent(ComponentType::rigidBody)))
      {
        rigidBody->setVelocity({ 0, 0, 0 });
      }
    }
  }
}
