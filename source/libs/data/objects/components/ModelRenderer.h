#ifndef MODELRENDERER_H
#define MODELRENDERER_H

#include "Component.h"
#include <uuid.h>

class ModelRenderer final : public Component {
public:
  ModelRenderer();

  [[nodiscard]] bool getShouldRender() const;
  void setShouldRender(bool shouldRender);

  [[nodiscard]] bool getUseStandardPipeline() const;
  void setUseStandardPipeline(bool useStandardPipeline);

  [[nodiscard]] uuids::uuid getModelUUID() const;
  void setModelUUID(const uuids::uuid& modelUUID);

  [[nodiscard]] uuids::uuid getTextureUUID() const;
  void setTextureUUID(const uuids::uuid& textureUUID);

  [[nodiscard]] uuids::uuid getSpecularMapUUID() const;
  void setSpecularMapUUID(const uuids::uuid& specularMapUUID);

  [[nodiscard]] bool canRender() const;

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

private:
  // The renderer, the vke::RenderObject, and the resolved asset handles are gone from the data: this
  // class holds only the asset UUIDs + flags. ECS3DRender resolves the UUIDs to GPU objects via the
  // GpuAssetCache, so the server can carry/replicate this data without ever touching Vulkan.
  bool m_shouldRender = false;
  bool m_useStandardPipeline = true;

  uuids::uuid m_modelUUID;
  uuids::uuid m_textureUUID;
  uuids::uuid m_specularMapUUID;
};



#endif //MODELRENDERER_H
