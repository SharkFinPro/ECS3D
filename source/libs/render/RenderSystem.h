#ifndef RENDERSYSTEM_H
#define RENDERSYSTEM_H

#include <memory>
#include <optional>
#include <unordered_map>
#include <uuid.h>

namespace vke {
  class PointLight;
  class SpotLight;
}

class ObjectManager;
class GpuAssetCache;

class RenderSystem {
public:
  // highlightUUID (the editor's selected object) is re-drawn with the objectHighlight pipeline. The
  // client passes nullopt.
  void variableUpdate(const ObjectManager& objectManager, GpuAssetCache& assetCache,
                      const std::optional<uuids::uuid>& highlightUUID = std::nullopt);

  // Drives the vke camera from a component Camera (Phase 4). Finds the active Camera object, builds a
  // view matrix from its Transform pose, disables the built-in free-fly camera, and pushes the pose into
  // the renderer. Falls back to (re-enabling) the free-fly camera when no active camera exists. The
  // client calls this each frame; the editor calls it only while its viewport is looking through a scene
  // camera, and calls useFreeFlyCamera() otherwise.
  // cameraObject optionally restricts the search to one object (Phase 4.4 — a client's own player camera);
  // nullopt means "first active camera in the scene".
  void updateCamera(const ObjectManager& objectManager, GpuAssetCache& assetCache,
                    const std::optional<uuids::uuid>& cameraObject = std::nullopt);

  // Hands the viewport back to the built-in free-fly camera (the editor's default view). Idempotent, so
  // it's safe to call every frame.
  void useFreeFlyCamera(GpuAssetCache& assetCache) const;

  // True for the object under the cursor; the editor reads it to drive Ctrl-click selection.
  [[nodiscard]] bool isSelected(const uuids::uuid& uuid) const;

private:
  struct CachedLight {
    std::shared_ptr<vke::PointLight> pointLight;
    std::shared_ptr<vke::SpotLight> spotLight;
  };

  // Render-side state keyed by the owning object. The vke lights are stateful (created once via the
  // lighting manager, updated each frame from the LightRenderer data).
  std::unordered_map<uuids::uuid, CachedLight> m_lights;

  std::unordered_map<uuids::uuid, bool> m_selected;
};



#endif //RENDERSYSTEM_H
