#include "Transform.h"
#include <imgui.h>
#include <memory>
#include "../Object.h"
#include "RigidBody.h"

Transform::Transform(const glm::vec3& position, const glm::vec3& scale, const glm::vec3& rotation)
  : Component(ComponentType::transform),
    initialPosition(position), livePosition(initialPosition), currentPosition(&initialPosition),
    initialScale(scale), liveScale(initialScale), currentScale(&initialScale),
    initialRotation(rotation), liveRotation(initialRotation), currentRotation(&initialRotation)
{}

glm::vec3 Transform::getPosition() const
{
  if (owner->getParent())
  {
    if (const auto& parentTransform = std::dynamic_pointer_cast<Transform>(owner->getParent()->getComponent(ComponentType::transform)))
    {
      return *parentTransform->currentPosition + *currentPosition;
    }
  }

  return *currentPosition;
}

glm::vec3 Transform::getScale() const
{
  if (owner->getParent())
  {
    if (const auto& parentTransform = std::dynamic_pointer_cast<Transform>(owner->getParent()->getComponent(ComponentType::transform)))
    {
      return *parentTransform->currentScale * *currentScale;
    }
  }

  return *currentScale;
}

glm::vec3 Transform::getRotation() const
{
  if (owner->getParent())
  {
    if (const auto& parentTransform = std::dynamic_pointer_cast<Transform>(owner->getParent()->getComponent(ComponentType::transform)))
    {
      return *parentTransform->currentRotation + *currentRotation;
    }
  }

  return *currentRotation;
}

void Transform::setScale(const glm::vec3 scale)
{
  *currentScale = scale;
}

void Transform::setRotation(const glm::vec3 rotation)
{
  *currentRotation = rotation;
}

void Transform::move(const glm::vec3& direction)
{
  *currentPosition += direction;
}

void Transform::displayGui()
{
  if (displayGuiHeader())
  {
    ImGui::PushID(1);
    ImGui::Text("Control Position:");
    ImGui::SliderFloat("x", &currentPosition->x, -30.0f, 30.0f);
    ImGui::SliderFloat("y", &currentPosition->y, -30.0f, 30.0f);
    ImGui::SliderFloat("z", &currentPosition->z, -30.0f, 30.0f);
    ImGui::PopID();

    ImGui::PushID(2);
    ImGui::Text("Control Rotation:");
    ImGui::SliderFloat("x", &currentRotation->x, 0.0f, 360.0f);
    ImGui::SliderFloat("y", &currentRotation->y, 0.0f, 360.0f);
    ImGui::SliderFloat("z", &currentRotation->z, 0.0f, 360.0f);
    ImGui::PopID();

    ImGui::PushID(3);
    ImGui::Text("Control Scale:");
    ImGui::SliderFloat("x", &currentScale->x, 0.1f, 10.0f);
    ImGui::SliderFloat("y", &currentScale->y, 0.1f, 10.0f);
    ImGui::SliderFloat("z", &currentScale->z, 0.1f, 10.0f);

    float combinedScale = (currentScale->x + currentScale->y + currentScale->z) / 3.0f;
    if (ImGui::SliderFloat("xyz", &combinedScale, 0.1f, 10.0f))
    {
      currentScale->x = currentScale->y = currentScale->z = combinedScale;
    }

    ImGui::PopID();
  }
}
