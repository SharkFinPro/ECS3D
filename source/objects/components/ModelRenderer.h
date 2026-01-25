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
  ModelRenderer(const std::shared_ptr<vke::VulkanEngine>& renderer,
                const std::shared_ptr<vke::Texture2D>& texture,
                const std::shared_ptr<vke::Texture2D>& specularMap,
                const std::shared_ptr<vke::Model>& model);

  explicit ModelRenderer(const std::shared_ptr<vke::VulkanEngine>& renderer);

  ~ModelRenderer() override = default;

  void variableUpdate(float dt) override;

  void displayGui() override;

  [[nodiscard]] bool selectedByRenderer() const;

  void renderHighlight() const;

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

private:
  std::shared_ptr<vke::RenderObject> m_renderObject = nullptr;
  std::weak_ptr<Transform> m_transform_ptr;

  std::shared_ptr<vke::VulkanEngine> m_renderer;
  std::shared_ptr<vke::Texture2D> m_texture = nullptr;
  std::shared_ptr<vke::Texture2D> m_specularMap = nullptr;
  std::shared_ptr<vke::Model> m_model = nullptr;

  bool m_useStandardPipeline = true;

  bool m_shouldRender = false;

  bool m_selectedByRenderer = false;

  void displayDragDrop(const char* label, const std::function<bool(const std::shared_ptr<Asset>&)>& setter);

  void displayTextureDragDrop();
  void displaySpecularDragDrop();
  void displayModelDragDrop();

  [[nodiscard]] bool canRender() const;
};



#endif //MODELRENDERER_H
