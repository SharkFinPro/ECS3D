#include "ModelAsset.h"
#include "AssetManager.h"
#include "../ECS3D.h"
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/assets/AssetManager.h>
#include <imgui.h>

ModelAsset::ModelAsset(const std::string& path)
  : Asset(path), m_path(path)
{}

std::string ModelAsset::getPath()
{
  return m_path;
}

std::shared_ptr<vke::Model> ModelAsset::getModel()
{
  return m_model;
}

void ModelAsset::load()
{
  m_model = assetManager->getECS()->getRenderer()->getAssetManager()->loadModel(m_path.c_str());
}

void ModelAsset::displayGui()
{
  Asset::displayGui();

  ImGui::Button("Model", {150, 150});
}
