#ifndef TEXTUREASSET_H
#define TEXTUREASSET_H

#include "Asset.h"
#include <uuid.h>
#include <VulkanEngine/components/assets/textures/Texture2D.h>

class TextureAsset final : public Asset {
public:
  explicit TextureAsset(uuids::uuid uuid,
                        std::string path);

  ~TextureAsset() override = default;

  std::string getPath();

  std::shared_ptr<vke::Texture2D> getTexture();

  void load() override;

  void displayGui() override;

  [[nodiscard]] nlohmann::json serialize() override;

private:
  std::string m_path;

  std::shared_ptr<vke::Texture2D> m_texture;
};



#endif //TEXTUREASSET_H
