#include "CameraSelection.h"
#include "Object.h"
#include "ObjectManager.h"
#include "components/Camera.h"
#include "components/Component.h"
#include "components/PlayerController.h"
#include "components/Transform.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

std::shared_ptr<Object> findActiveCamera(const ObjectManager& objectManager, const std::optional<uuids::uuid>& only)
{
  for (const auto& object : objectManager.getAllObjects())
  {
    if (only && object->getUUID() != *only)
    {
      continue;
    }

    const auto camera = object->getComponent<Camera>(ComponentType::camera);

    if (!camera || !camera->isActive())
    {
      continue;
    }

    if (!object->getComponent<Transform>(ComponentType::transform))
    {
      continue;
    }

    return object;
  }

  return nullptr;
}

glm::vec3 cameraForward(const glm::vec3& eulerDegrees, const glm::vec3& direction)
{
  const glm::quat orientation(glm::radians(eulerDegrees));
  const glm::vec3 turned = orientation * direction;

  if (glm::length(turned) < 1e-6f)
  {
    return { 0.0f, 0.0f, -1.0f };
  }

  return glm::normalize(turned);
}

glm::vec3 cameraUp(const glm::vec3& forward)
{
  if (glm::abs(glm::dot(forward, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.9999f)
  {
    return { 0.0f, 0.0f, 1.0f };
  }

  return { 0.0f, 1.0f, 0.0f };
}

std::optional<CameraView> cameraViewOf(const Object& object)
{
  const auto camera = object.getComponent<Camera>(ComponentType::camera);
  const auto transform = object.getComponent<Transform>(ComponentType::transform);

  if (!camera || !transform)
  {
    return std::nullopt;
  }

  const glm::vec3 position = transform->getPosition();
  const glm::vec3 forward = cameraForward(transform->getRotation(), camera->getDirection());

  return CameraView{
    position,
    glm::lookAt(position, position + forward, cameraUp(forward)),
    camera->getFov(),
    camera->getNearPlane(),
    camera->getFarPlane()
  };
}

std::string cameraLabel(const Object& object)
{
  std::string label = object.getName();

  if (const auto playerController = object.getComponent<PlayerController>(ComponentType::playerController))
  {
    label += " (Player " + std::to_string(playerController->getPlayerSlot()) + ")";
  }

  if (const auto camera = object.getComponent<Camera>(ComponentType::camera); camera && !camera->isActive())
  {
    label += " - inactive";
  }

  return label;
}

std::string cameraPreviewLabel(const ObjectManager* objectManager, const std::optional<uuids::uuid>& chosen,
                               const char* freeFlyLabel)
{
  if (objectManager && chosen)
  {
    if (const auto object = objectManager->getObjectByUUID(*chosen))
    {
      return cameraLabel(*object);
    }
  }

  return freeFlyLabel;
}
