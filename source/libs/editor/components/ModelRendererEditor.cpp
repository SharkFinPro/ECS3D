#include "ModelRendererEditor.h"
#include "../ComponentEditor.h"
#include <memory>

class Component;

void registerModelRendererEditor(ComponentEditor& componentEditor)
{
  componentEditor.registerHandler("ModelRenderer", [](const std::shared_ptr<Component>& component) {
    // TODO: migrate ModelRenderer::displayGui here. Cast to ModelRenderer (ECS3DData), draw the
    // TODO:   "Use Standard Pipeline"/"Render" checkboxes and the Texture/Specular/Model asset
    // TODO:   drag-drop targets. Drag-drop now sets the asset UUID on the data (setModelUUID, ...)
    // TODO:   instead of building a vke::RenderObject; the AssetBrowserPanel supplies the dragged uuid.
    (void)component;
  });
}
