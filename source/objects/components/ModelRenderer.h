#ifndef MODELRENDERER_H
#define MODELRENDERER_H

#include "Component.h"
#include <VulkanEngine/VulkanEngine.h>
#include <memory>

class Transform;
class Asset;

class ModelRenderer final : public Component {
public:
  ModelRenderer(const std::shared_ptr<VulkanEngine>& renderer, const std::shared_ptr<Texture>& texture,
                         const std::shared_ptr<Texture>& specularMap, const std::shared_ptr<Model>& model);

  explicit ModelRenderer(const std::shared_ptr<VulkanEngine>& renderer);

  ~ModelRenderer() override = default;

  void variableUpdate(float dt) override;

  void displayGui() override;

private:
  std::shared_ptr<RenderObject> renderObject;
  std::weak_ptr<Transform> transform_ptr;

  std::shared_ptr<VulkanEngine> renderer;
  std::shared_ptr<Texture> texture;
  std::shared_ptr<Texture> specularMap;
  std::shared_ptr<Model> model;

  bool shouldRender;

  void displayDragDrop(const char* label, const std::function<bool(const std::shared_ptr<Asset>&)>& setter);

  void displayTextureDragDrop();
  void displaySpecularDragDrop();
  void displayModelDragDrop();

  [[nodiscard]] bool canRender() const;
};



#endif //MODELRENDERER_H
