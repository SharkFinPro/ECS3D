#include "RigidBodyEditor.h"
#include "../ComponentEditor.h"
#include <memory>

class Component;

void registerRigidBodyEditor(ComponentEditor& componentEditor)
{
  componentEditor.registerHandler("RigidBody", [](const std::shared_ptr<Component>& component) {
    // TODO: migrate RigidBody::displayGui here. Cast component to RigidBody (ECS3DData), draw the
    // TODO:   header via ComponentEditor::displayHeader, then the "Do Gravity" checkbox, "Gravity"
    // TODO:   input, and "Friction"/"Mass" sliders. Editing here writes initial values, which the
    // TODO:   editor sends to the server as edit commands rather than mutating the live sim directly.
    (void)component;
  });
}
