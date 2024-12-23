#ifndef TEXTUREASSET_H
#define TEXTUREASSET_H

#include "Asset.h"
#include <VulkanEngine/objects/Texture.h>

class TextureAsset final : public Asset {
public:
  explicit TextureAsset(const std::string& path);
  ~TextureAsset() override = default;

  std::string getPath();

  std::shared_ptr<Texture> getTexture();

  void load() override;

  void displayGui() override;

private:
  std::string path;

  std::shared_ptr<Texture> texture;
};



#endif //TEXTUREASSET_H
