#ifndef RENDERCONTEXT_H
#define RENDERCONTEXT_H

#include <memory>

namespace vke {
  class VulkanEngine;
}

class RenderContext {
public:
  virtual ~RenderContext() = default;

  // TODO: expose the renderer + GPU asset cache that ModelRenderer/LightRenderer need,
  // TODO:   replacing the getRenderer()->getRenderingManager()->getRenderer3D() chain.

  [[nodiscard]] virtual std::shared_ptr<vke::VulkanEngine> getRenderer() const = 0;
};



#endif //RENDERCONTEXT_H
