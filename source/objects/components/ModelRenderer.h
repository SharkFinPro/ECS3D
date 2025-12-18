#ifndef MODELRENDERER_H
#define MODELRENDERER_H

#include "Component.h"
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/assets/objects/RenderObject.h>
#include <VulkanEngine/components/assets/objects/Model.h>
#include <VulkanEngine/components/assets/textures/Texture2D.h>
#include <memory>
#include <functional>

class Asset;
class Transform;

class ModelRenderer final : public Component {
public:
  ModelRenderer(const std::shared_ptr<vke::VulkanEngine>& renderer, const std::shared_ptr<vke::Texture2D>& texture,
                         const std::shared_ptr<vke::Texture2D>& specularMap, const std::shared_ptr<vke::Model>& model);

  explicit ModelRenderer(const std::shared_ptr<vke::VulkanEngine>& renderer);

  ~ModelRenderer() override = default;

  void variableUpdate(float dt) override;

  void displayGui() override;

  [[nodiscard]] bool selectedByRenderer() const;

  void renderHighlight() const;

private:
  std::shared_ptr<vke::RenderObject> renderObject;
  std::weak_ptr<Transform> transform_ptr;

  std::shared_ptr<vke::VulkanEngine> renderer;
  std::shared_ptr<vke::Texture2D> texture;
  std::shared_ptr<vke::Texture2D> specularMap;
  std::shared_ptr<vke::Model> model;

  bool useStandardPipeline = true;

  bool shouldRender;

  bool m_selectedByRenderer = false;

  void displayDragDrop(const char* label, const std::function<bool(const std::shared_ptr<Asset>&)>& setter);

  void displayTextureDragDrop();
  void displaySpecularDragDrop();
  void displayModelDragDrop();

  [[nodiscard]] bool canRender() const;
};



#endif //MODELRENDERER_H
