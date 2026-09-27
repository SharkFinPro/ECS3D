#ifndef RENDERSYSTEM_H
#define RENDERSYSTEM_H

#include <glm/mat4x4.hpp>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <uuid.h>

namespace vke {
  class PointLight;
  class SpotLight;
  class VulkanEngine;
}

class ObjectManager;
class GpuAssetCache;

class RenderSystem {
public:
  // Every object in highlightUUIDs (the editor's selected objects) is re-drawn with the objectHighlight
  // pipeline. The client passes none.
  void variableUpdate(const ObjectManager& objectManager, GpuAssetCache& assetCache,
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

  // True for the object under the cursor; the editor reads it to drive Ctrl-click selection.
  [[nodiscard]] bool isSelected(const uuids::uuid& uuid) const;

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
  // Only the kind the LightRenderer currently uses is ever populated; toggling spot/point releases
  // the old shared_ptr (and its vke light) before creating the new one.
  struct CachedLight {
    std::shared_ptr<vke::PointLight> pointLight;
    std::shared_ptr<vke::SpotLight> spotLight;
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

  std::unordered_map<uuids::uuid, bool> m_selected;

  std::optional<ProjectionParams> m_appliedProjection;
  std::optional<ProjectionParams> m_rejectedProjection;

  // Which camera viewParams() should read from: true once enableFreeFlyCamera has (re-)taken over,
  // false the frame updateCamera pushes a component camera's pose - see m_componentCameraView.
  bool m_freeFlyActive = true;
  glm::mat4 m_componentCameraView{ 1.0f };

  // Reused across calls to avoid a per-frame allocation: variableUpdate refills it with every uuid
  // seen this frame, then prunes m_lights/m_selected/GpuAssetCache's per-object caches of any uuid
  // that is no longer present (object deleted, scene switched, project reloaded).
  std::unordered_set<uuids::uuid> m_liveUUIDs;
};



#endif //RENDERSYSTEM_H
