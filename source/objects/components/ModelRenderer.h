#ifndef MODELRENDERER_H
#define MODELRENDERER_H

#include "Component.h"
#include <VulkanEngine/VulkanEngine.h>
#include <memory>
#include <unordered_map>

class Transform;

class ModelRenderer final : public Component {
public:
  explicit ModelRenderer(const std::shared_ptr<VulkanEngine>& renderer, const char* texturePath,
                         const char* specularMapPath, const char* modelPath);
  ~ModelRenderer() override = default;

  void variableUpdate(float dt) override;

  void enableRendering();

  void disableRendering();

private:
  std::shared_ptr<RenderObject> renderObject;
  std::weak_ptr<Transform> transform_ptr;

  std::unordered_map<const char*, std::shared_ptr<Texture>> textures;
  std::unordered_map<const char*, std::shared_ptr<Texture>> specularMaps;
  std::unordered_map<const char*, std::shared_ptr<Model>> models;
};



#endif //MODELRENDERER_H
