#include "RenderSystem.h"
#include "GpuAssetCache.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/Transform.h>
#include <objects/components/ModelRenderer.h>
#include <objects/components/LightRenderer.h>
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/assets/objects/RenderObject.h>
#include <VulkanEngine/components/pipelines/implementations/common/PipelineTypes.h>
#include <VulkanEngine/components/renderingManager/RenderingManager.h>
#include <VulkanEngine/components/renderingManager/renderer3D/Renderer3D.h>

void RenderSystem::variableUpdate(ObjectManager& objectManager, GpuAssetCache& assetCache)
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
      // The UUID -> RenderObject resolution that used to happen in ModelRenderer (build a vke object
      // from the asset shared_ptrs) is now a cache lookup; the data only carries the UUIDs.
      const auto renderObject = assetCache.getRenderObject(modelRenderer->getModelUUID(),
                                                           modelRenderer->getTextureUUID(),
                                                           modelRenderer->getSpecularMapUUID());

      if (renderObject)
      {
        renderObject->setPosition(transform->getPosition());
        renderObject->setScale(transform->getScale());
        renderObject->setOrientationEuler(transform->getRotation());

        // TODO: the 4th arg was &m_selectedByRenderer (editor selection feedback). That render-side
        // TODO:   state needs a home (keyed per ModelRenderer) before the editor highlight works again.
        assetCache.getRenderer()->getRenderingManager()->getRenderer3D()->renderObject(
          renderObject,
          modelRenderer->getUseStandardPipeline() ? vke::PipelineType::object : vke::PipelineType::ellipticalDots,
          nullptr
        );
      }
    }

    if (const auto lightRenderer = object->getComponent<LightRenderer>(ComponentType::lightRenderer);
        lightRenderer)
    {
      // TODO: build/cache a vke::PointLight or SpotLight (per isSpotLight()) from the light data via
      // TODO:   the lighting manager, position it from the transform, and submit with renderLight().
      // TODO:   Unlike RenderObjects these are stateful in the engine, so the cache must own one light
      // TODO:   per LightRenderer rather than rebuild each frame.
    }
  }
}
