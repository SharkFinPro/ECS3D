#ifndef RENDERSYSTEM_H
#define RENDERSYSTEM_H

#include "LiveObjectTracker.h"
#include "TerrainShadowVotes.h"
#include "TerrainSideVotes.h"
#include <glm/mat4x4.hpp>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <uuid.h>

namespace vke {
  class DirectionalLight;
  class PointLight;
  class RenderObject;
  class SpotLight;
  class VolumeSurface;
  class VulkanEngine;
}

namespace terrain {
  class TerrainStore;
}

class ObjectManager;
class GpuAssetCache;

class RenderSystem {
public:
  // The store whose terrain palettes the terrain surfaces are drawn with; without one every terrain uses the
  // engine's default flat colors.
  void setTerrainStore(std::shared_ptr<const terrain::TerrainStore> terrainStore);

  // Every object in highlightUUIDs (the editor's selected objects) is outlined. The client passes none.
  void variableUpdate(const ObjectManager& objectManager, GpuAssetCache& assetCache,
                      std::span<const uuids::uuid> highlightUUIDs = {});

  // Editor-only: draws a camera-facing, constant-screen-size sprite at every light. Call it each frame after
  // the camera update (updateCamera/useFreeFlyCamera) so the sprites face this frame's view. The client never
  // calls it, so it never shows them. Every light in highlightUUIDs is outlined in the selection color, as a
  // selected model is. A pick of a sprite reports the light's uuid through requestPick. In free-fly the view is
  // the camera's pose before this frame's render() moves it, so sprites trail a moving camera by one frame (the
  // viewport transform gizmo behaves the same).
  void drawLightGizmos(const ObjectManager& objectManager, GpuAssetCache& assetCache,
                       std::span<const uuids::uuid> highlightUUIDs = {});

  // Drives the vke camera from a component Camera. Finds the active Camera object, builds a
  // view matrix from its Transform pose, disables the built-in free-fly camera, and pushes the pose into
  // the renderer. Falls back to (re-enabling) the free-fly camera when no active camera exists. The
  // client calls this each frame; the editor calls it only while its viewport is looking through a scene
  // camera, and calls useFreeFlyCamera() otherwise.
  // cameraObject optionally restricts the search to one object (a client's own player camera);
  // nullopt means "first active camera in the scene".
  void updateCamera(const ObjectManager& objectManager, GpuAssetCache& assetCache,
                    const std::optional<uuids::uuid>& cameraObject = std::nullopt);

  // Hands the viewport back to the built-in free-fly camera (the editor's default view). Idempotent, so
  // it's safe to call every frame.
  void useFreeFlyCamera(GpuAssetCache& assetCache);

  // Shows or hides the renderer's ground grid. Cheap enough to call every frame.
  static void setGridVisible(GpuAssetCache& assetCache, bool visible);

  // Asks the renderer what is under the cursor and calls onPicked once, from inside the render() that resolves it
  // (a few frames after the next render()), with the uuid of the object that owns the picked model, light sprite
  // or terrain. It receives nullopt over empty space, off the scene, or for an object that was destroyed meanwhile.
  // The renderer picks only while a request is pending, and the draws of those frames are the only ones that
  // record an owner, so an idle frame pays nothing. The result names an owner as of the picked frame: it can be
  // gone from the scene by the time the callback runs.
  void requestPick(GpuAssetCache& assetCache, std::function<void(std::optional<uuids::uuid>)> onPicked);

  // What the viewport is currently looking through: the last view matrix updateCamera pushed for a
  // component camera, or the free-fly camera's own live view matrix when no component camera is active.
  // Projection is the last values applied (or rejected) by applyProjection, else the free-fly defaults -
  // the same fallback updateCamera/enableFreeFlyCamera use before the first frame. The editor's viewport
  // gizmo builds its gizmo::View from this rather than reaching into vke itself.
  struct ViewParams {
    glm::mat4 view{ 1.0f };
    float fovDegrees;
    float nearPlane;
    float farPlane;
  };

  [[nodiscard]] ViewParams viewParams(const GpuAssetCache& assetCache) const;

private:
  // Only the kind the LightRenderer currently uses is ever populated; changing its type releases the
  // old shared_ptr (and its vke light) before creating the new one.
  struct CachedLight {
    std::shared_ptr<vke::PointLight> pointLight;
    std::shared_ptr<vke::SpotLight> spotLight;
    std::shared_ptr<vke::DirectionalLight> directionalLight;
  };

  // The values last pushed to (or rejected by) Renderer3D::setProjectionParameters, so a per-frame call
  // only reaches the renderer when the active camera's fov/near/far actually changed, and a persistently
  // invalid triple is only logged once instead of every frame.
  struct ProjectionParams {
    float fov;
    float nearPlane;
    float farPlane;

    bool operator==(const ProjectionParams&) const = default;
  };

  // Applies params to Renderer3D unless they match what was last applied or last rejected. Renderer3D
  // throws std::invalid_argument for a degenerate triple; that is caught and logged rather than crashing,
  // since it is a safety net - the Camera component's own setters already clamp to a valid range.
  void applyProjection(const std::shared_ptr<vke::VulkanEngine>& renderer, const ProjectionParams& params);

  // Re-enables the built-in free-fly camera if it was disabled, and always applies the free-fly
  // projection default - even when the camera was already enabled, so switching back from a component
  // camera still resets the projection it may have changed.
  void enableFreeFlyCamera(const std::shared_ptr<vke::VulkanEngine>& renderer);

  // Render-side state keyed by the owning object. The vke lights are stateful (created once via the
  // lighting manager, updated each frame from the LightRenderer data).
  std::unordered_map<uuids::uuid, CachedLight> m_lights;

  // Which object each drawn render object or surface belongs to, recorded only while a pick request is in flight.
  // Shared with the request callbacks, which clear it when the last one resolves.
  struct PickOwners {
    std::unordered_map<const void*, uuids::uuid> owners;
    std::size_t requestsInFlight = 0;
  };

  void notePickOwner(const void* drawn, const uuids::uuid& owner) const;

  std::shared_ptr<PickOwners> m_pickOwners = std::make_shared<PickOwners>();

  // renderVolumeSurface only makes a surface pickable when given a flag; the engine writes it, nothing reads it.
  bool m_pickSink = false;

  std::optional<ProjectionParams> m_appliedProjection;
  std::optional<ProjectionParams> m_rejectedProjection;

  // Which camera viewParams() should read from: true once enableFreeFlyCamera has (re-)taken over,
  // false the frame updateCamera pushes a component camera's pose - see m_componentCameraView.
  bool m_freeFlyActive = true;
  glm::mat4 m_componentCameraView{ 1.0f };

  // variableUpdate adds every uuid it draws; m_lights and GpuAssetCache's per-object caches are
  // pruned of absent uuids (object deleted, scene switched, project reloaded) only when that set changes.
  LiveObjectTracker m_liveObjects;

  std::vector<std::shared_ptr<vke::RenderObject>> m_selectionOutlines;

  // The terrains drawn this frame; kept as a member so the set does not allocate every frame.
  std::unordered_set<uuids::uuid> m_drawnTerrains;

  // Terrain submissions wait until the loop ends so every vote for a surface's shadow flag is in first.
  struct TerrainDraw {
    std::shared_ptr<vke::VolumeSurface> surface;
    glm::mat4 model;
    uuids::uuid objectUUID;
    uuids::uuid terrainUUID;
  };

  std::vector<TerrainDraw> m_terrainDraws;
  TerrainShadowVotes m_terrainShadowVotes;
  TerrainSideVotes m_terrainSideVotes;
  std::shared_ptr<const terrain::TerrainStore> m_terrainStore;
};



#endif //RENDERSYSTEM_H
