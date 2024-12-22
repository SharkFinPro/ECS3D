#ifndef ASSETMANAGER_H
#define ASSETMANAGER_H

#include <vector>
#include <memory>

class ECS3D;
class Asset;

class AssetManager {
public:
  void setECS(ECS3D* ecs);

  [[nodiscard]] ECS3D* getECS() const;

  void displayGui();

private:
  ECS3D* ecs;
  std::vector<std::shared_ptr<Asset>> assets;
};



#endif //ASSETMANAGER_H
