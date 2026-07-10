#include "RenderSystem.h"
#include "GpuAssetCache.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/Transform.h>
#include <objects/components/ModelRenderer.h>
#include <objects/components/LightRenderer.h>
#include <objects/components/Camera.h>
#include <objects/components/collisions/BoxCollider.h>
#include <objects/components/collisions/SphereCollider.h>
#include <glm/vec3.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/camera/Camera.h>
#include <VulkanEngine/components/assets/objects/RenderObject.h>
#include <VulkanEngine/components/lighting/LightingManager.h>
#include <VulkanEngine/components/lighting/lights/PointLight.h>
#include <VulkanEngine/components/lighting/lights/SpotLight.h>
#include <VulkanEngine/components/pipelines/implementations/common/PipelineTypes.h>
#include <VulkanEngine/components/renderingManager/RenderingManager.h>
#include <VulkanEngine/components/renderingManager/renderer3D/Renderer3D.h>

void RenderSystem::variableUpdate(const ObjectManager& objectManager, GpuAssetCache& assetCache,
                                 const std::optional<uuids::uuid>& highlightUUID)
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
      const auto renderObject = assetCache.getRenderObject(uuid,
                                                           modelRenderer->getModelUUID(),
                                                           modelRenderer->getTextureUUID(),
                                                           modelRenderer->getSpecularMapUUID());

      if (renderObject)
      {
        renderObject->setPosition(transform->getPosition());
        renderObject->setScale(transform->getScale());
        renderObject->setOrientationEuler(transform->getRotation());
        renderObject->setReflectivity(modelRenderer->getReflectivity());

        // pointer is stable: unordered_map keeps element references valid across rehash.
        renderer->getRenderingManager()->getRenderer3D()->renderObject(
          renderObject,
          modelRenderer->getUseStandardPipeline() ? vke::PipelineType::object : vke::PipelineType::ellipticalDots,
          &m_selected[uuid]
        );

        // The editor's selected object gets a second pass with the highlight pipeline (an outline).
        if (highlightUUID == uuid)
        {
          renderer->getRenderingManager()->getRenderer3D()->renderObject(renderObject, vke::PipelineType::objectHighlight);
        }
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

    // Collider debug gizmo: draw the collider's shape (offset by its local transform) with the
    // highlight pipeline when its render flag is on.
    if (const auto box = object->getComponent<BoxCollider>(ComponentType::collider); box && box->getRenderCollider())
    {
      if (const auto gizmo = assetCache.getColliderGizmo(uuid, "assets/models/cube_1x1x1.glb"))
      {
        gizmo->setPosition(transform->getPosition() + box->getLocalPosition());
        gizmo->setScale(transform->getScale() * box->getLocalScale());
        gizmo->setOrientationEuler(transform->getRotation() + box->getLocalRotation());

        renderer->getRenderingManager()->getRenderer3D()->renderObject(gizmo, vke::PipelineType::objectHighlight);
      }
    }
    else if (const auto sphere = object->getComponent<SphereCollider>(ComponentType::collider); sphere && sphere->getRenderCollider())
    {
      if (const auto gizmo = assetCache.getColliderGizmo(uuid, "assets/models/sphere_3.glb"))
      {
        gizmo->setPosition(transform->getPosition() + sphere->getLocalPosition());
        gizmo->setScale(transform->getScale() * sphere->getLocalRadius());

        renderer->getRenderingManager()->getRenderer3D()->renderObject(gizmo, vke::PipelineType::objectHighlight);
      }
    }
  }
}

void RenderSystem::updateCamera(const ObjectManager& objectManager, GpuAssetCache& assetCache,
                                const std::optional<uuids::uuid>& cameraObject)
{
  const auto renderer = assetCache.getRenderer();

  for (const auto& object : objectManager.getAllObjects())
  {
    // When a specific camera object is requested (a client's own player camera), skip the rest.
    if (cameraObject && object->getUUID() != *cameraObject)
    {
      continue;
    }

    const auto camera = object->getComponent<Camera>(ComponentType::camera);

    if (!camera || !camera->isActive())
    {
      continue;
    }

    const auto transform = object->getComponent<Transform>(ComponentType::transform);

    if (!transform)
    {
      continue;
    }

    // Pose comes from the object's Transform. A zero rotation faces -Z (matching object orientation and
    // the vke camera's default forward); the quaternion also carries roll into the up vector.
    const glm::vec3 position = transform->getPosition();
    const glm::quat orientation(glm::radians(transform->getRotation()));
    const glm::vec3 forward = orientation * glm::vec3(0.0f, 0.0f, -1.0f);
    const glm::vec3 up = orientation * glm::vec3(0.0f, 1.0f, 0.0f);

    const glm::mat4 viewMatrix = lookAt(position, position + forward, up);

    // Take over from the built-in free-fly camera (render() skips it while disabled, so this pose sticks).
    renderer->getCamera()->disable();
    renderer->getRenderingManager()->getRenderer3D()->setCameraParameters(position, viewMatrix);
    return;
  }

  // No active component camera in the scene — hand control back to the built-in free-fly camera.
  renderer->getCamera()->enable();
}

bool RenderSystem::isSelected(const uuids::uuid& uuid) const
{
  const auto it = m_selected.find(uuid);

  return it != m_selected.end() && it->second;
}
