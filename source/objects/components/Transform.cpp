#include "Transform.h"
#include <imgui.h>
#include <memory>
#include "../Object.h"
#include "RigidBody.h"

Transform::Transform(const glm::vec3& position, const glm::vec3& scale, const glm::vec3& rotation)
  : Component(ComponentType::transform), updateID(0),
    initialPosition(position), livePosition(initialPosition), currentPosition(&initialPosition),
    initialScale(scale), liveScale(initialScale), currentScale(&initialScale),
    initialRotation(rotation), liveRotation(initialRotation), currentRotation(&initialRotation)
{}

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
      return parentTransform->getPosition() + *currentPosition;
    }
  }

  return *currentPosition;
}

glm::vec3 Transform::getScale() const
{
  if (owner->getParent())
  {
    if (const auto& parentTransform = owner->getParent()->getComponent<Transform>(ComponentType::transform))
    {
      return parentTransform->getScale() * *currentScale;
    }
  }

  return *currentScale;
}

glm::vec3 Transform::getRotation() const
{
  if (owner->getParent())
  {
    if (const auto& parentTransform = owner->getParent()->getComponent<Transform>(ComponentType::transform))
    {
      return parentTransform->getRotation() + *currentRotation;
    }
  }

  return *currentRotation;
}

void Transform::setScale(const glm::vec3 scale)
{
  *currentScale = scale;
  updateID++;
}

void Transform::setRotation(const glm::vec3 rotation)
{
  *currentRotation = rotation;
  updateID++;
}

void Transform::move(const glm::vec3& direction)
{
  *currentPosition += direction;
  updateID++;
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

    updateID++;

    ImGui::PopID();
  }
}

void Transform::start()
{
  livePosition = initialPosition;
  liveScale = initialScale;
  liveRotation = initialRotation;

  currentPosition = &livePosition;
  currentScale = &liveScale;
  currentRotation = &liveRotation;

  updateID++;
}

void Transform::stop()
{
  currentPosition = &initialPosition;
  currentScale = &initialScale;
  currentRotation = &initialRotation;

  updateID++;
}
