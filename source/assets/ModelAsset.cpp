#include "ModelAsset.h"
#include "AssetManager.h"
#include "../ECS3D.h"
#include <VulkanEngine/VulkanEngine.h>

ModelAsset::ModelAsset(const std::string& path)
  : Asset(path), path(path)
{}

std::string ModelAsset::getPath()
{
  return path;
}

std::shared_ptr<Model> ModelAsset::getModel()
{
  return model;
}

void ModelAsset::load()
{
  const auto renderer = assetManager->getECS()->getRenderer();

  model = renderer->loadModel(path.c_str());
}

void ModelAsset::displayGui()
{
  Asset::displayGui();

  ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));
  ImGui::Selectable("Model", false, 0, {150, 150});
  ImGui::PopStyleVar();
}
