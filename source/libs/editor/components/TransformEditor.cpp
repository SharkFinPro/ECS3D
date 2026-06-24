#include "TransformEditor.h"
#include "../ComponentEditor.h"
#include <memory>

class Component;

void registerTransformEditor(ComponentEditor& componentEditor)
{
  componentEditor.registerHandler("Transform", [](const std::shared_ptr<Component>& component) {
    // TODO: migrate Transform::displayGui here. Cast component to Transform (ECS3DData), draw the
    // TODO:   header via ComponentEditor::displayHeader, then gc::xyzGui for Position/Rotation/Scale
    // TODO:   and the "Scale All" drag. Bump the transform's updateID when edited. gc::xyzGui +
    // TODO:   imgui still live in core (GuiComponents.h) and need migrating into this library.
    (void)component;
  });
}
