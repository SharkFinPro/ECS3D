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
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <Log.h>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/camera/Camera.h>
#include <VulkanEngine/components/assets/objects/RenderObject.h>
#include <VulkanEngine/components/lighting/LightingManager.h>
#include <VulkanEngine/components/lighting/lights/PointLight.h>
#include <VulkanEngine/components/lighting/lights/SpotLight.h>
#include <VulkanEngine/components/pipelines/implementations/common/PipelineTypes.h>
#include <VulkanEngine/components/renderingManager/RenderingManager.h>
#include <VulkanEngine/components/renderingManager/renderer3D/Renderer3D.h>

namespace {
  // A zero (or near-zero) direction has no defined forward: normalize() would return NaN and a lookAt
  // built from it degenerates. Fall back to a stable default instead.
  glm::vec3 guardDirection(const glm::vec3& direction, const glm::vec3& fallback)
  {
    if (glm::length(direction) < 1e-6f)
    {
      return fallback;
    }

    return glm::normalize(direction);
  }

  // Renderer3D's own defaults before setProjectionParameters is ever called; named here so the editor's
  // free-fly view keeps exactly today's look.
  constexpr float defaultFreeFlyFovDegrees = 45.0f;
  constexpr float defaultFreeFlyNearPlane = 0.1f;
  constexpr float defaultFreeFlyFarPlane = 1000.0f;
}

void RenderSystem::variableUpdate(const ObjectManager& objectManager, GpuAssetCache& assetCache,
                                 std::span<const uuids::uuid> highlightUUIDs)
{
  const auto renderer = assetCache.getRenderer();
  const auto lightingManager = renderer->getLightingManager();

  m_liveUUIDs.clear();

  for (const auto& object : objectManager.getAllObjects())
  {
    const auto transform = object->getComponent<Transform>(ComponentType::transform);

    if (!transform)
    {
      continue;
    }

    const auto uuid = object->getUUID();

    m_liveUUIDs.insert(uuid);

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

        // The editor's selected objects get a second pass with the highlight pipeline (an outline).
        if (std::ranges::find(highlightUUIDs, uuid) != highlightUUIDs.end())
        {
          renderer->getRenderingManager()->getRenderer3D()->renderObject(renderObject, vke::PipelineType::objectHighlight);
        }
      }
    }

    if (const auto lightRenderer = object->getComponent<LightRenderer>(ComponentType::lightRenderer);
        lightRenderer)
    {
      auto& light = m_lights[uuid];

      // Create only the kind currently in use. A LightRenderer that toggles spot/point releases the
      // other kind's shared_ptr here first, dropping its own vke light (and shadow map) rather than
      // keeping both allocated for as long as the object exists.
      if (lightRenderer->isSpotLight())
      {
        if (!light.spotLight)
        {
          light.pointLight.reset();

          light.spotLight = std::dynamic_pointer_cast<vke::SpotLight>(lightingManager->createSpotLight(
            glm::vec3(0), lightRenderer->getColor(), lightRenderer->getAmbient(), lightRenderer->getDiffuse(), lightRenderer->getSpecular()));
        }
      }
      else if (!light.pointLight)
      {
        light.spotLight.reset();

        light.pointLight = std::dynamic_pointer_cast<vke::PointLight>(lightingManager->createPointLight(
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
        light.spotLight->setDirection(guardDirection(lightRenderer->getDirection(), glm::vec3(0.0f, -1.0f, 0.0f)));
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
        gizmo->setPosition(box->getPosition());
        gizmo->setScale(box->getScale());
        gizmo->setOrientationEuler(box->getRotation());

        renderer->getRenderingManager()->getRenderer3D()->renderObject(gizmo, vke::PipelineType::objectHighlight);
      }
    }
    else if (const auto sphere = object->getComponent<SphereCollider>(ComponentType::collider); sphere && sphere->getRenderCollider())
    {
      if (const auto gizmo = assetCache.getColliderGizmo(uuid, "assets/models/sphere_3.glb"))
      {
        gizmo->setPosition(sphere->getPosition());
        gizmo->setScale(glm::vec3(sphere->getRadius()));

        renderer->getRenderingManager()->getRenderer3D()->renderObject(gizmo, vke::PipelineType::objectHighlight);
      }
    }
  }

  // Release every render resource keyed by a uuid no longer in the scene (object deleted, scene
  // switched, project reloaded) - otherwise these caches grow for the life of the process, and a
  // stale light stays registered with the lighting manager forever. Safe here: this runs before this
  // frame's draws are recorded (variableUpdate always precedes vke::VulkanEngine::render()), and every
  // vke object being dropped (Light, RenderObject) blocks on vkDeviceWaitIdle in its own destructor
  // (see VulkanEngine's Light::~Light and UniformBuffer::~UniformBuffer), so nothing here can race a
  // command buffer that is still using it.
  std::erase_if(m_lights, [this](const auto& entry) { return !m_liveUUIDs.contains(entry.first); });
  std::erase_if(m_selected, [this](const auto& entry) { return !m_liveUUIDs.contains(entry.first); });

  assetCache.pruneStale(m_liveUUIDs);
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

    // Position comes from the Transform; facing is the Camera's own direction, rotated by the object's
    // orientation so the camera turns as the object turns. World-up (not an orientation-derived up) keeps
    // the horizon level and avoids the roll/inversion the euler-quaternion up produced.
    const glm::vec3 position = transform->getPosition();
    const glm::quat orientation(glm::radians(transform->getRotation()));

    const glm::vec3 forward = guardDirection(orientation * camera->getDirection(), glm::vec3(0.0f, 0.0f, -1.0f));

    // lookAt degenerates when the view direction is parallel to up (looking straight up/down); fall back to
    // a different reference axis so the matrix stays finite.
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    if (glm::abs(glm::dot(forward, up)) > 0.9999f)
    {
      up = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    const glm::mat4 viewMatrix = lookAt(position, position + forward, up);

    // Take over from the built-in free-fly camera (render() skips it while disabled, so this pose sticks).
    renderer->getCamera()->disable();
    renderer->getRenderingManager()->getRenderer3D()->setCameraParameters(position, viewMatrix);
    applyProjection(renderer, { camera->getFov(), camera->getNearPlane(), camera->getFarPlane() });
    return;
  }

  // No active component camera in the scene - hand control back to the built-in free-fly camera.
  enableFreeFlyCamera(renderer);
}

void RenderSystem::applyProjection(const std::shared_ptr<vke::VulkanEngine>& renderer, const ProjectionParams& params)
{
  if (m_appliedProjection == params || m_rejectedProjection == params)
  {
    return;
  }

  try
  {
    renderer->getRenderingManager()->getRenderer3D()->setProjectionParameters(params.fov, params.nearPlane, params.farPlane);
    m_appliedProjection = params;
    m_rejectedProjection.reset();
  }
  catch (const std::invalid_argument& e)
  {
    Log::warn(LogCategory::engine, "Rejected camera projection (fov=" + std::to_string(params.fov) +
      ", near=" + std::to_string(params.nearPlane) + ", far=" + std::to_string(params.farPlane) + "): " + e.what());
    m_rejectedProjection = params;
  }
}

void RenderSystem::enableFreeFlyCamera(const std::shared_ptr<vke::VulkanEngine>& renderer)
{
  // Hand the viewport back to the built-in free-fly camera. render() only pushes the free-fly pose while
  // the scene view is focused, so push it once here too: otherwise the component camera's last pose would
  // linger until the user happens to focus the viewport.
  const auto camera = renderer->getCamera();

  if (!camera->isEnabled())
  {
    camera->enable();
    renderer->getRenderingManager()->getRenderer3D()->setCameraParameters(camera->getPosition(), camera->getViewMatrix());
  }

  // Always reconciled, even when the camera itself was already enabled, so switching back from a
  // component camera (which may have changed the projection) still resets it.
  applyProjection(renderer, { defaultFreeFlyFovDegrees, defaultFreeFlyNearPlane, defaultFreeFlyFarPlane });
}

void RenderSystem::useFreeFlyCamera(GpuAssetCache& assetCache)
{
  enableFreeFlyCamera(assetCache.getRenderer());
}

bool RenderSystem::isSelected(const uuids::uuid& uuid) const
{
  const auto it = m_selected.find(uuid);

  return it != m_selected.end() && it->second;
}
