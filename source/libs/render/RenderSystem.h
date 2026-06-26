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

  // The renderer's pick feedback (was ModelRenderer::selectedByRenderer) - true for the object under
  // the cursor; the editor reads it to drive Ctrl-click selection.
  [[nodiscard]] bool isSelected(const uuids::uuid& uuid) const;

private:
  struct CachedLight {
    std::shared_ptr<vke::PointLight> pointLight;
    std::shared_ptr<vke::SpotLight> spotLight;
  };

  // Render-side state keyed by the owning object. The vke lights are stateful (created once via the
  // lighting manager, updated each frame from the LightRenderer data). m_selected holds the
  // renderer's pick feedback (the old ModelRenderer::m_selectedByRenderer) the editor reads back.
  std::unordered_map<uuids::uuid, CachedLight> m_lights;

  std::unordered_map<uuids::uuid, bool> m_selected;
};



#endif //RENDERSYSTEM_H
