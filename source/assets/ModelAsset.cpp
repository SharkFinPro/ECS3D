#include "ModelAsset.h"
#include "AssetManager.h"
#include "../ECS3D.h"
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/assets/AssetManager.h>
#include <imgui.h>

ModelAsset::ModelAsset(const std::string& path)
  : Asset(path), path(path)
{}

std::string ModelAsset::getPath()
{
  return path;
}

std::shared_ptr<vke::Model> ModelAsset::getModel()
{
  return model;
}

void ModelAsset::load()
{
  const auto renderer = assetManager->getECS()->getRenderer();

  model = renderer->getAssetManager()->loadModel(path.c_str());
}

void ModelAsset::displayGui()
{
  Asset::displayGui();

  ImGui::Button("Model", {150, 150});
}
