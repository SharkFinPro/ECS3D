#include "TextureAsset.h"

TextureAsset::TextureAsset(std::string path)
  : Asset(path), path(path)
{
}

std::string TextureAsset::getPath()
{
  return path;
}

std::shared_ptr<Texture> TextureAsset::getTexture()
{
  return texture;
}
