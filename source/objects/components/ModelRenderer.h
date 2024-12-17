#ifndef MODELRENDERER_H
#define MODELRENDERER_H

#include "Component.h"
#include <VulkanEngine/VulkanEngine.h>
#include <memory>

class Transform;

class ModelRenderer final : public Component {
public:
  explicit ModelRenderer(const std::shared_ptr<VulkanEngine>& renderer, const std::shared_ptr<Texture>& texture,
                         const std::shared_ptr<Texture>& specularMap, const std::shared_ptr<Model>& model);

  ~ModelRenderer() override = default;

  void variableUpdate(float dt) override;

  void enableRendering() const;

  void disableRendering() const;

private:
  std::shared_ptr<RenderObject> renderObject;
  std::weak_ptr<Transform> transform_ptr;
};



#endif //MODELRENDERER_H
