#ifndef ASSETMANAGER_H
#define ASSETMANAGER_H

#include <unordered_map>
#include <memory>
#include <string>

class ECS3D;
class Asset;
class TextureAsset;
class ModelAsset;

class AssetManager {
public:
  explicit AssetManager(ECS3D* ecs);

  [[nodiscard]] ECS3D* getECS() const;

  void displayGui();

  void loadTexture(const std::string& path);

  void loadModel(const std::string& path);

  std::shared_ptr<Asset> getAsset(const std::string& path);

  std::shared_ptr<TextureAsset> getTexture(const std::string& path);

  std::shared_ptr<ModelAsset> getModel(const std::string& path);

private:
  ECS3D* ecs;
  std::unordered_map<std::string, std::shared_ptr<Asset>> assets;
};



#endif //ASSETMANAGER_H
