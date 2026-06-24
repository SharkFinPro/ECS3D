#include "LightRendererEditor.h"
#include "../ComponentEditor.h"
#include <memory>

class Component;

void registerLightRendererEditor(ComponentEditor& componentEditor)
{
  componentEditor.registerHandler("LightRenderer", [](const std::shared_ptr<Component>& component) {
    // TODO: migrate LightRenderer::displayGui here. Cast to LightRenderer (ECS3DData), draw the
    // TODO:   "Spot Light" checkbox, color picker, ambient/diffuse/specular sliders, direction, and
    // TODO:   cone angle. These now read/write the plain light data directly (setColor, setAmbient,
    // TODO:   ...) instead of poking a vke::PointLight/SpotLight.
    (void)component;
  });
}
