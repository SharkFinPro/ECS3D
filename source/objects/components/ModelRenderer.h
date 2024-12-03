#ifndef MODELRENDERER_H
#define MODELRENDERER_H

#include "Component.h"
#include <VulkanEngine/VulkanEngine.h>
#include <memory>

class Transform;

class ModelRenderer final : public Component {
public:
  explicit ModelRenderer(const std::shared_ptr<VulkanEngine>& renderer, const char* texturePath,
                         const char* specularMapPath, const char* modelPath);
  ~ModelRenderer() override = default;

  void variableUpdate(float dt) override;

private:
  std::shared_ptr<RenderObject> renderObject;
  std::weak_ptr<Transform> transform_ptr;
};



#endif //MODELRENDERER_H
