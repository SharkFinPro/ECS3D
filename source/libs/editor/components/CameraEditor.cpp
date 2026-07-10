#include "CameraEditor.h"
#include "../ComponentEditor.h"
#include "../GuiComponents.h"
#include <objects/components/Camera.h>
#include <imgui.h>
#include <algorithm>
#include <memory>

void registerCameraEditor(ComponentEditor& componentEditor)
{
  // Keyed by the componentTypeToString display name ("Camera") ObjectGUIManager looks the handler up by.
  componentEditor.registerHandler("Camera", [](const std::shared_ptr<Component>& component) -> bool {
    const auto camera = std::dynamic_pointer_cast<Camera>(component);
    if (!camera)
    {
      return false;
    }

    bool edited = false;

    if (ComponentEditor::displayHeader(component))
    {
      bool active = camera->isActive();
      float fov = camera->getFov();
      float nearPlane = camera->getNearPlane();
      float farPlane = camera->getFarPlane();

      // Only an active camera is picked up by RenderSystem::updateCamera. Note: fov/near/far are carried
      // and serialized, but the engine's projection is currently hardcoded, so editing them has no visible
      // effect until VulkanEngine exposes a projection setter (see ROADMAP 4.2).
      if (gc::accentCheckbox("Active", &active))
      {
        camera->setActive(active);
        edited = true;
      }

      if (gc::accentSlider("FOV", &fov, 1.0f, 179.0f))
      {
        camera->setFov(fov);
        edited = true;
      }

      if (gc::labeledDrag("Near Plane", &nearPlane, 0.01f))
      {
        camera->setNearPlane(std::max(nearPlane, 0.001f));
        edited = true;
      }

      if (gc::labeledDrag("Far Plane", &farPlane, 1.0f))
      {
        camera->setFarPlane(std::max(farPlane, nearPlane + 0.001f));
        edited = true;
      }
    }

    return edited;
  });
}
