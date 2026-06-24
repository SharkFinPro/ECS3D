#include "RenderSystem.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/Transform.h>
#include <objects/components/ModelRenderer.h>
#include <objects/components/LightRenderer.h>

void RenderSystem::variableUpdate(ObjectManager& objectManager, RenderContext& context)
{
  for (const auto& object : objectManager.getAllObjects())
  {
    const auto transform = object->getComponent<Transform>(ComponentType::transform);

    if (!transform)
    {
      continue;
    }

    if (const auto modelRenderer = object->getComponent<ModelRenderer>(ComponentType::modelRenderer);
        modelRenderer && modelRenderer->getShouldRender() && modelRenderer->canRender())
    {
      // TODO: resolve the vke::RenderObject for the model/texture/specular UUIDs from the
      // TODO:   GpuAssetCache, set its position/scale/orientation from the transform, then submit via
      // TODO:   context.getRenderer()->getRenderingManager()->getRenderer3D()->renderObject(...). The
      // TODO:   pipeline comes from getUseStandardPipeline(). renderHighlight + selectedByRenderer
      // TODO:   (editor selection feedback) also move here.
    }

    if (const auto lightRenderer = object->getComponent<LightRenderer>(ComponentType::lightRenderer);
        lightRenderer)
    {
      // TODO: build/update a vke::PointLight or SpotLight (per isSpotLight()) from the light data,
      // TODO:   position it from the transform, then submit via
      // TODO:   context.getRenderer()->getLightingManager()->renderLight(...).
    }
  }
}
