#include "TransformEditor.h"
#include "../ComponentEditor.h"
#include "../GuiComponents.h"
#include <objects/components/Transform.h>
#include <glm/vec3.hpp>
#include <imgui.h>
#include <memory>

void registerTransformEditor(ComponentEditor& componentEditor)
{
  componentEditor.registerHandler("Transform", [](const std::shared_ptr<Component>& component) -> bool {
    const auto transform = std::dynamic_pointer_cast<Transform>(component);
    if (!transform)
    {
      return false;
    }

    bool edited = false;

    if (ComponentEditor::displayHeader(component))
    {
      // The editor edits this object's own (local) transform; the systems handle the parent-combined
      // world transform.
      glm::vec3 position = transform->getLocalPosition();
      glm::vec3 rotation = transform->getLocalRotation();
      glm::vec3 scale = transform->getLocalScale();

      if (gc::xyzGuiBoxed("Position", &position.x, &position.y, &position.z))
      {
        transform->setPosition(position);
        edited = true;
      }

      if (gc::xyzGuiBoxed("Rotation", &rotation.x, &rotation.y, &rotation.z))
      {
        transform->setRotation(rotation);
        edited = true;
      }

      if (gc::xyzGuiBoxed("Scale", &scale.x, &scale.y, &scale.z))
      {
        transform->setScale(scale);
        edited = true;
      }

      float combinedScale = (scale.x + scale.y + scale.z) / 3.0f;
      if (gc::accentSlider("Scale All", &combinedScale, 0.0f, 10.0f))
      {
        transform->setScale(glm::vec3(combinedScale));
        edited = true;
      }
    }

    return edited;
  });
}
