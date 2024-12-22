#ifndef ASSETMANAGER_H
#define ASSETMANAGER_H

#include <vector>
#include <memory>
#include <string>

class ECS3D;
class Asset;

class AssetManager {
public:
  explicit AssetManager(ECS3D* ecs);

  [[nodiscard]] ECS3D* getECS() const;

  void displayGui();

  void loadTexture(const std::string& path);

private:
  ECS3D* ecs;
  std::vector<std::shared_ptr<Asset>> assets;
};



#endif //ASSETMANAGER_H
