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

  [[nodiscard]] float getReflectivity() const;
  void setReflectivity(float reflectivity);

  [[nodiscard]] uuids::uuid getModelUUID() const;
  void setModelUUID(const uuids::uuid& modelUUID);

  [[nodiscard]] uuids::uuid getTextureUUID() const;
  void setTextureUUID(const uuids::uuid& textureUUID);

  [[nodiscard]] uuids::uuid getSpecularMapUUID() const;
  void setSpecularMapUUID(const uuids::uuid& specularMapUUID);

  [[nodiscard]] bool canRender() const;

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

  void pack(net::Message& message) const override;

  void unpack(net::MessageReader& messageReader) override;

private:
  bool m_shouldRender = false;
  bool m_useStandardPipeline = true;

  float m_reflectivity = 0.0f;

  uuids::uuid m_modelUUID;
  uuids::uuid m_textureUUID;
  uuids::uuid m_specularMapUUID;
};



#endif //MODELRENDERER_H
