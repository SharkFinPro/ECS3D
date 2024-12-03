#ifndef MODELRENDERER_H
#define MODELRENDERER_H

#include "Component.h"
#include <VulkanEngine/VulkanEngine.h>
#include <memory>

class ModelRenderer final : public Component {
public:
  explicit ModelRenderer(const std::shared_ptr<VulkanEngine>& renderer);
  ~ModelRenderer() override = default;

  void variableUpdate(float dt) override;

private:
  std::shared_ptr<RenderObject> renderObject;
};



#endif //MODELRENDERER_H
