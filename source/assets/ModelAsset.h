#ifndef MODELASSET_H
#define MODELASSET_H

#include "Asset.h"
#include <memory>
#include <VulkanEngine/components/objects/Model.h>

class ModelAsset final : public Asset {
public:
  explicit ModelAsset(const std::string& path);
  ~ModelAsset() override = default;

  std::string getPath();

  std::shared_ptr<vke::Model> getModel();

  void load() override;

  void displayGui() override;

private:
  std::string path;

  std::shared_ptr<vke::Model> model;
};



#endif //MODELASSET_H
