#include "RenderSystem.h"
#include "GpuAssetCache.h"
#include "Billboard.h"
#include "CapsuleGizmo.h"
#include "OutlineColors.h"
#include "RenderComponents.h"
#include "TerrainSurfacePlan.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/CameraSelection.h>
#include <objects/components/Component.h>
#include <objects/components/Transform.h>
#include <objects/components/ModelRenderer.h>
#include <objects/components/LightRenderer.h>
#include <objects/components/Camera.h>
#include <objects/components/collisions/BoxCollider.h>
#include <objects/components/collisions/CapsuleCollider.h>
#include <objects/components/collisions/SphereCollider.h>
#include <objects/components/collisions/ConvexMeshCollider.h>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
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
#include <VulkanEngine/components/assets/volume/VolumeSurface.h>
#include <VulkanEngine/components/lighting/LightingManager.h>
#include <VulkanEngine/components/lighting/lights/PointLight.h>
#include <VulkanEngine/components/lighting/lights/DirectionalLight.h>
#include <VulkanEngine/components/lighting/lights/SpotLight.h>
#include <VulkanEngine/components/pipelines/implementations/common/PipelineTypes.h>
#include <VulkanEngine/components/renderingManager/RenderingManager.h>
#include <VulkanEngine/components/renderingManager/renderer3D/MousePicker.h>
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

  // A capsule outline is a cylinder between the segment's ends plus a sphere on each end, all in one color so
  // the outline mask merges them into a single silhouette. The end spheres are gizmo parts 0 and 1 and the
  // cylinder is part 2, so a capsule with no core segment needs only the first two. Returns the part count.
  std::size_t drawCapsuleGizmo(const std::shared_ptr<vke::VulkanEngine>& renderer, GpuAssetCache& assetCache,
                        const uuids::uuid& uuid, CapsuleCollider& capsule)
  {
    const auto [start, end] = capsule.getSegment();
    const auto layout = layoutCapsuleGizmo(start, end, capsule.getRadius());
    const auto& renderer3D = renderer->getRenderingManager()->getRenderer3D();

    for (std::size_t i = 0; i < layout.capPositions.size(); ++i)
    {
      if (const auto cap = assetCache.getColliderGizmo(uuid, "assets/models/sphere_3.glb", i))
      {
        cap->setPosition(layout.capPositions[i]);
        cap->setScale(layout.capScale);

        renderer3D->renderOutline(cap, colliderOutlineColor);
      }
    }

    if (!layout.hasCylinder)
    {
      return layout.capPositions.size();
    }

    if (const auto cylinder = assetCache.getColliderGizmo(uuid, "assets/models/cylinder.glb", layout.capPositions.size()))
    {
      cylinder->setPosition(layout.cylinderPosition);
      cylinder->setScale(layout.cylinderScale);
      cylinder->setOrientationEuler(capsule.getRotation());

      renderer3D->renderOutline(cylinder, colliderOutlineColor);
    }

    return layout.capPositions.size() + 1;
  }
}

void RenderSystem::variableUpdate(const ObjectManager& objectManager, GpuAssetCache& assetCache,
                                 std::span<const uuids::uuid> highlightUUIDs)
{
  const auto renderer = assetCache.getRenderer();
  const auto lightingManager = renderer->getLightingManager();

  assetCache.applyPendingReset();

  m_liveObjects.beginFrame();
  m_selectionOutlines.clear();
  m_drawnTerrains.clear();
  m_terrainDraws.clear();
  m_terrainShadowVotes.clear();
  m_terrainSideVotes.clear();

  // Open sides change a surface's geometry, so every vote is in before the first surface is fetched. Only the
  // collider slot and the transform are looked up, with the draw loop's conditions.
  for (const auto& object : objectManager.getAllObjects())
  {
    const auto& owned = object->getComponents();
    const auto collider = owned.find(ComponentType::collider);

    if (collider == owned.end() || collider->second->getSubType() != ComponentType::SubComponentType_terrain ||
        !owned.contains(ComponentType::transform))
    {
      continue;
    }

    const auto* terrainComponent = static_cast<const Terrain*>(collider->second.get());

    if (!terrainComponent->getTerrainUUID().is_nil() && terrainComponent->volume())
    {
      m_terrainSideVotes.vote(terrainComponent->getTerrainUUID(), terrainComponent->getOpenSides());
    }
  }

  for (const auto& object : objectManager.getAllObjects())
  {
    const auto components = gatherRenderComponents(*object);
    const auto transform = components.transform;

    if (!transform)
    {
      continue;
    }

    const auto uuid = object->getUUID();

    m_liveObjects.add(uuid);

    if (const auto modelRenderer = components.modelRenderer;
        modelRenderer && modelRenderer->getShouldRender() && modelRenderer->canRender())
    {
      const auto renderObject = assetCache.getRenderObject(uuid,
                                                           modelRenderer->getModelUUID(),
                                                           modelRenderer->getTextureUUID(),
                                                           modelRenderer->getSpecularMapUUID());

      if (renderObject)
      {
        if (assetCache.claimTransformPush(uuid, transform->getWorldUpdateID()))
        {
          renderObject->setPosition(transform->getPosition());
          renderObject->setScale(transform->getScale());
          renderObject->setOrientationEuler(transform->getRotation());
        }

        renderObject->setReflectivity(modelRenderer->getReflectivity());

        renderer->getRenderingManager()->getRenderer3D()->renderPickableObject(
          renderObject,
          modelRenderer->getUseStandardPipeline() ? vke::PipelineType::object : vke::PipelineType::ellipticalDots
        );

        notePickOwner(renderObject.get(), uuid);

        // The editor's selected objects get a screen-space outline in the selection color, submitted
        // after the loop (see below).
        if (std::ranges::find(highlightUUIDs, uuid) != highlightUUIDs.end())
        {
          m_selectionOutlines.push_back(renderObject);
        }
      }
    }

    if (const auto lightRenderer = components.lightRenderer; lightRenderer)
    {
      auto& light = m_lights[uuid];

      // Create only the kind currently in use. A LightRenderer that changes type releases the other
      // kinds' shared_ptrs here first, dropping their vke lights (and shadow maps) rather than keeping
      // all three allocated for as long as the object exists.
      const auto lightType = lightRenderer->getLightType();

      if (lightType != LightType::point)
      {
        light.pointLight.reset();
      }

      if (lightType != LightType::spot)
      {
        light.spotLight.reset();
      }

      if (lightType != LightType::directional)
      {
        light.directionalLight.reset();
      }

      if (lightType == LightType::spot)
      {
        if (!light.spotLight)
        {
          light.spotLight = std::dynamic_pointer_cast<vke::SpotLight>(lightingManager->createSpotLight(
            glm::vec3(0), lightRenderer->getColor(), lightRenderer->getAmbient(), lightRenderer->getDiffuse(), lightRenderer->getSpecular()));
        }
      }
      else if (lightType == LightType::directional)
      {
        if (!light.directionalLight)
        {
          light.directionalLight = std::dynamic_pointer_cast<vke::DirectionalLight>(
            lightingManager->createDirectionalLight(guardDirection(lightRenderer->getDirection(), glm::vec3(0.0f, -1.0f, 0.0f)),
                                                    lightRenderer->getColor(), lightRenderer->getAmbient(),
                                                    lightRenderer->getDiffuse(), lightRenderer->getSpecular()));
        }
      }
      else if (!light.pointLight)
      {
        light.pointLight = std::dynamic_pointer_cast<vke::PointLight>(lightingManager->createPointLight(
          glm::vec3(0), lightRenderer->getColor(), lightRenderer->getAmbient(), lightRenderer->getDiffuse(), lightRenderer->getSpecular()));
      }

      // Push the data values into the engine light each frame (data is the source of truth), then
      // position it from the transform and submit the active one.
      if (lightType == LightType::spot)
      {
        light.spotLight->setColor(lightRenderer->getColor());
        light.spotLight->setAmbient(lightRenderer->getAmbient());
        light.spotLight->setDiffuse(lightRenderer->getDiffuse());
        light.spotLight->setSpecular(lightRenderer->getSpecular());
        light.spotLight->setDirection(guardDirection(lightRenderer->getDirection(), glm::vec3(0.0f, -1.0f, 0.0f)));
        light.spotLight->setConeAngle(lightRenderer->getConeAngle());
        light.spotLight->setPosition(transform->getPosition());
        light.spotLight->setRange(lightRenderer->getRange());

        lightingManager->renderLight(light.spotLight);
      }
      else if (lightType == LightType::directional)
      {
        light.directionalLight->setColor(lightRenderer->getColor());
        light.directionalLight->setAmbient(lightRenderer->getAmbient());
        light.directionalLight->setDiffuse(lightRenderer->getDiffuse());
        light.directionalLight->setSpecular(lightRenderer->getSpecular());
        light.directionalLight->setDirection(guardDirection(lightRenderer->getDirection(), glm::vec3(0.0f, -1.0f, 0.0f)));
        light.directionalLight->setShadowDistance(lightRenderer->getShadowDistance());

        lightingManager->renderLight(light.directionalLight);
      }
      else
      {
        light.pointLight->setColor(lightRenderer->getColor());
        light.pointLight->setAmbient(lightRenderer->getAmbient());
        light.pointLight->setDiffuse(lightRenderer->getDiffuse());
        light.pointLight->setSpecular(lightRenderer->getSpecular());
        light.pointLight->setPosition(transform->getPosition());
        light.pointLight->setRange(lightRenderer->getRange());

        lightingManager->renderLight(light.pointLight);
      }
    }

    // Collider debug gizmo: draw the collider's shape (offset by its local transform) as an
    // outline only, so the object inside stays visible, when its render flag is on.
    std::size_t gizmoParts = 0;
    bool drawsHullGizmo = false;
    if (const auto box = components.box; box && box->getRenderCollider())
    {
      if (const auto gizmo = assetCache.getColliderGizmo(uuid, "assets/models/cube_1x1x1.glb"))
      {
        gizmo->setPosition(box->getPosition());
        gizmo->setScale(box->getScale());
        gizmo->setOrientationEuler(box->getRotation());

        renderer->getRenderingManager()->getRenderer3D()->renderOutline(gizmo, colliderOutlineColor);
      }

      gizmoParts = 1;
    }
    else if (const auto sphere = components.sphere; sphere && sphere->getRenderCollider())
    {
      if (const auto gizmo = assetCache.getColliderGizmo(uuid, "assets/models/sphere_3.glb"))
      {
        gizmo->setPosition(sphere->getPosition());
        gizmo->setScale(glm::vec3(sphere->getRadius()));

        renderer->getRenderingManager()->getRenderer3D()->renderOutline(gizmo, colliderOutlineColor);
      }

      gizmoParts = 1;
    }
    else if (const auto capsule = components.capsule; capsule && capsule->getRenderCollider())
    {
      gizmoParts = drawCapsuleGizmo(renderer, assetCache, uuid, *capsule);
    }
    else if (const auto convex = components.convexMesh; convex && convex->getRenderCollider())
    {
      drawsHullGizmo = true;

      if (const auto gizmo = assetCache.getHullGizmo(uuid, convex->getHull()))
      {
        gizmo->setPosition(convex->getPosition());
        gizmo->setScale(convex->getScale());
        gizmo->setOrientationEuler(convex->getRotation());

        renderer->getRenderingManager()->getRenderer3D()->renderOutline(gizmo, colliderOutlineColor);
      }
    }

    assetCache.trimColliderGizmo(uuid, gizmoParts);

    if (!drawsHullGizmo)
    {
      assetCache.dropHullGizmo(uuid);
    }

    if (const auto terrainComponent = components.terrain;
        terrainComponent && !terrainComponent->getTerrainUUID().is_nil())
    {
      if (const auto volume = terrainComponent->volume())
      {
        const auto& terrainUUID = terrainComponent->getTerrainUUID();

        if (const auto surface = assetCache.getTerrainSurface(terrainUUID, volume,
                                                            m_terrainSideVotes.openSides(terrainUUID)))
        {
          m_terrainDraws.push_back({
            surface,
            terrain::terrainModelMatrix(transform->getPosition(), transform->getOrientation()),
            uuid,
            terrainUUID
          });
          m_terrainShadowVotes.vote(terrainUUID, terrainComponent->getCastShadows());
        }

        m_drawnTerrains.insert(terrainUUID);
      }
    }
  }

  assetCache.trimTerrainSurfaces(m_drawnTerrains);

  const auto renderer3D = renderer->getRenderingManager()->getRenderer3D();

  for (const auto& draw : m_terrainDraws)
  {
    draw.surface->setCastsShadows(m_terrainShadowVotes.castsShadows(draw.terrainUUID));
    renderer3D->renderVolumeSurface(draw.surface, draw.model, &m_pickSink);
    notePickOwner(draw.surface.get(), draw.objectUUID);
  }

  // The later submission owns an overlap in the outline mask, so selection outlines go last to win over
  // any collider gizmo outline.
  for (const auto& selected : m_selectionOutlines)
  {
    renderer3D->renderOutline(selected, selectionOutlineColor);
  }
  m_selectionOutlines.clear();

  for (const auto& draw : m_terrainDraws)
  {
    if (std::ranges::find(highlightUUIDs, draw.objectUUID) != highlightUUIDs.end())
    {
      renderer3D->renderOutline(draw.surface, draw.model, selectionOutlineColor);
    }
  }
  m_terrainDraws.clear();

  // Release every render resource keyed by a uuid no longer in the scene (object deleted, scene
  // switched, project reloaded) - otherwise these caches grow for the life of the process, and a
  // stale light stays registered with the lighting manager forever. Safe here: this runs before this
  // frame's draws are recorded (variableUpdate always precedes vke::VulkanEngine::render()), and every
  // vke object being dropped (Light, RenderObject) blocks on vkDeviceWaitIdle in its own destructor
  // (see VulkanEngine's Light::~Light and UniformBuffer::~UniformBuffer), so nothing here can race a
  // command buffer that is still using it.
  // Every per-uuid cache is filled only for uuids added in the loop above, so pruning matters only when that
  // set changes.
  if (m_liveObjects.endFrame())
  {
    const auto& liveUUIDs = m_liveObjects.liveUUIDs();

    std::erase_if(m_lights, [&liveUUIDs](const auto& entry) { return !liveUUIDs.contains(entry.first); });

    assetCache.pruneStale(liveUUIDs);
  }
}

void RenderSystem::drawLightGizmos(const ObjectManager& objectManager, GpuAssetCache& assetCache,
                                   std::span<const uuids::uuid> highlightUUIDs)
{
  const auto renderer = assetCache.getRenderer();
  const auto renderer3D = renderer->getRenderingManager()->getRenderer3D();
  const auto [view, fovDegrees, nearPlane, farPlane] = viewParams(assetCache);
  const glm::quat orientation = billboardOrientation(view);

  for (const auto& object : objectManager.getAllObjects())
  {
    const auto components = gatherRenderComponents(*object);
    const auto transform = components.transform;

    if (!transform || !components.lightRenderer)
    {
      continue;
    }

    const glm::vec3 position = transform->getPosition();
    const auto size = billboardWorldSize(view, position, fovDegrees, nearPlane, lightGizmoScreenFraction);

    if (!size)
    {
      continue;
    }

    const auto uuid = object->getUUID();
    const auto gizmo = assetCache.getLightGizmo(uuid);

    if (!gizmo)
    {
      continue;
    }

    gizmo->setPosition(position);
    gizmo->setOrientationQuat(orientation);
    gizmo->setScale(*size);

    // A pick of the sprite names the light's object, so the editor selects the light the way it selects a model.
    renderer3D->renderPickableObject(gizmo, vke::PipelineType::texturedPlane);
    notePickOwner(gizmo.get(), uuid);

    if (std::ranges::find(highlightUUIDs, uuid) != highlightUUIDs.end())
    {
      renderer3D->renderOutline(gizmo, selectionOutlineColor);
    }
  }
}

void RenderSystem::updateCamera(const ObjectManager& objectManager, GpuAssetCache& assetCache,
                                const std::optional<uuids::uuid>& cameraObject)
{
  const auto renderer = assetCache.getRenderer();

  // When a specific camera object is requested (a client's own player camera), only it is considered.
  if (const auto object = findActiveCamera(objectManager, cameraObject))
  {
    if (const auto cameraView = cameraViewOf(*object))
    {
      // Take over from the built-in free-fly camera (render() skips it while disabled, so this pose sticks).
      renderer->getCamera()->disable();
      renderer->getRenderingManager()->getRenderer3D()->setCameraParameters(cameraView->position, cameraView->view);
      applyProjection(renderer, { cameraView->fov, cameraView->nearPlane, cameraView->farPlane });

      m_freeFlyActive = false;
      m_componentCameraView = cameraView->view;
      return;
    }
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

  m_freeFlyActive = true;
}

void RenderSystem::useFreeFlyCamera(GpuAssetCache& assetCache)
{
  enableFreeFlyCamera(assetCache.getRenderer());
}

void RenderSystem::setGridVisible(GpuAssetCache& assetCache, const bool visible)
{
  const auto renderer3D = assetCache.getRenderer()->getRenderingManager()->getRenderer3D();

  if (visible)
  {
    renderer3D->enableGrid();
  }
  else
  {
    renderer3D->disableGrid();
  }
}

void RenderSystem::notePickOwner(const void* drawn, const uuids::uuid& owner) const
{
  if (m_pickOwners->requestsInFlight > 0)
  {
    m_pickOwners->owners.try_emplace(drawn, owner);
  }
}

void RenderSystem::requestPick(GpuAssetCache& assetCache, std::function<void(std::optional<uuids::uuid>)> onPicked)
{
  const auto picker = assetCache.getRenderer()->getRenderingManager()->getRenderer3D()->getMousePicker();

  ++m_pickOwners->requestsInFlight;

  picker->requestPick([state = m_pickOwners, onPicked = std::move(onPicked)](const std::optional<vke::PickResult>& result)
  {
    std::optional<uuids::uuid> owner;

    if (result.has_value())
    {
      const void* drawn = result->renderObject ? static_cast<const void*>(result->renderObject.get())
                                               : static_cast<const void*>(result->volumeSurface.get());

      if (const auto it = state->owners.find(drawn); it != state->owners.end())
      {
        owner = it->second;
      }
    }

    if (--state->requestsInFlight == 0)
    {
      state->owners.clear();
    }

    onPicked(owner);
  });
}

RenderSystem::ViewParams RenderSystem::viewParams(const GpuAssetCache& assetCache) const
{
  const auto renderer = assetCache.getRenderer();

  const glm::mat4 view = m_freeFlyActive ? renderer->getCamera()->getViewMatrix() : m_componentCameraView;

  const auto projection = m_appliedProjection.value_or(
    ProjectionParams{ defaultFreeFlyFovDegrees, defaultFreeFlyNearPlane, defaultFreeFlyFarPlane });

  return { view, projection.fov, projection.nearPlane, projection.farPlane };
}
