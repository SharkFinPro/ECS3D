#include "RenderSystem.h"
#include "GpuAssetCache.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/Transform.h>
#include <objects/components/ModelRenderer.h>
#include <objects/components/LightRenderer.h>
#include <glm/vec3.hpp>
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/assets/objects/RenderObject.h>
#include <VulkanEngine/components/lighting/LightingManager.h>
#include <VulkanEngine/components/lighting/lights/PointLight.h>
#include <VulkanEngine/components/lighting/lights/SpotLight.h>
#include <VulkanEngine/components/pipelines/implementations/common/PipelineTypes.h>
#include <VulkanEngine/components/renderingManager/RenderingManager.h>
#include <VulkanEngine/components/renderingManager/renderer3D/Renderer3D.h>

void RenderSystem::variableUpdate(ObjectManager& objectManager, GpuAssetCache& assetCache)
{
  const auto renderer = assetCache.getRenderer();
  const auto lightingManager = renderer->getLightingManager();

  for (const auto& object : objectManager.getAllObjects())
  {
    const auto transform = object->getComponent<Transform>(ComponentType::transform);

    if (!transform)
    {
      continue;
    }

    const auto uuid = object->getUUID();

    if (const auto modelRenderer = object->getComponent<ModelRenderer>(ComponentType::modelRenderer);
        modelRenderer && modelRenderer->getShouldRender() && modelRenderer->canRender())
    {
      // The UUID -> RenderObject resolution that used to happen in ModelRenderer (build a vke object
      // from the asset shared_ptrs) is now a cache lookup; the data only carries the UUIDs.
      const auto renderObject = assetCache.getRenderObject(uuid,
                                                           modelRenderer->getModelUUID(),
                                                           modelRenderer->getTextureUUID(),
                                                           modelRenderer->getSpecularMapUUID());

      if (renderObject)
      {
        renderObject->setPosition(transform->getPosition());
        renderObject->setScale(transform->getScale());
        renderObject->setOrientationEuler(transform->getRotation());

        // 4th arg is the renderer's pick feedback (was ModelRenderer::m_selectedByRenderer); the
        // pointer is stable because unordered_map keeps element references valid across rehash.
        renderer->getRenderingManager()->getRenderer3D()->renderObject(
          renderObject,
          modelRenderer->getUseStandardPipeline() ? vke::PipelineType::object : vke::PipelineType::ellipticalDots,
          &m_selected[uuid]
        );
      }
    }

    if (const auto lightRenderer = object->getComponent<LightRenderer>(ComponentType::lightRenderer);
        lightRenderer)
    {
      auto& light = m_lights[uuid];

      if (!light.pointLight)
      {
        light.pointLight = std::dynamic_pointer_cast<vke::PointLight>(lightingManager->createPointLight(
          glm::vec3(0), lightRenderer->getColor(), lightRenderer->getAmbient(), lightRenderer->getDiffuse(), lightRenderer->getSpecular()));

        light.spotLight = std::dynamic_pointer_cast<vke::SpotLight>(lightingManager->createSpotLight(
          glm::vec3(0), lightRenderer->getColor(), lightRenderer->getAmbient(), lightRenderer->getDiffuse(), lightRenderer->getSpecular()));
      }

      // Push the data values into the engine light each frame (data is the source of truth), then
      // position it from the transform and submit the active one.
      if (lightRenderer->isSpotLight())
      {
        light.spotLight->setColor(lightRenderer->getColor());
        light.spotLight->setAmbient(lightRenderer->getAmbient());
        light.spotLight->setDiffuse(lightRenderer->getDiffuse());
        light.spotLight->setSpecular(lightRenderer->getSpecular());
        light.spotLight->setDirection(lightRenderer->getDirection());
        light.spotLight->setConeAngle(lightRenderer->getConeAngle());
        light.spotLight->setPosition(transform->getPosition());

        lightingManager->renderLight(light.spotLight);
      }
      else
      {
        light.pointLight->setColor(lightRenderer->getColor());
        light.pointLight->setAmbient(lightRenderer->getAmbient());
        light.pointLight->setDiffuse(lightRenderer->getDiffuse());
        light.pointLight->setSpecular(lightRenderer->getSpecular());
        light.pointLight->setPosition(transform->getPosition());

        lightingManager->renderLight(light.pointLight);
      }
    }
  }

  // TODO: collider debug gizmo (BoxCollider/SphereCollider::variableUpdate, gated on getRenderCollider()).
  // TODO:   It needs a per-collider cached RenderObject (like the lights above) built from the debug
  // TODO:   model/texture by path (cube_1x1x1.glb / sphere_3.glb + white.png), positioned via the
  // TODO:   collider's getPosition/getScale/getRotation, submitted with the objectHighlight pipeline.
}

bool RenderSystem::isSelected(const uuids::uuid& uuid) const
{
  const auto it = m_selected.find(uuid);

  return it != m_selected.end() && it->second;
}
