#ifndef TEXTUREASSET_H
#define TEXTUREASSET_H

#include "Asset.h"
#include <VulkanEngine/components/assets/textures/Texture2D.h>

class TextureAsset final : public Asset {
public:
  explicit TextureAsset(const std::string& path);
  ~TextureAsset() override = default;

  std::string getPath();

  std::shared_ptr<vke::Texture2D> getTexture();

  void load() override;

  void displayGui() override;

private:
  std::string path;

  std::shared_ptr<vke::Texture2D> texture;
};



#endif //TEXTUREASSET_H
