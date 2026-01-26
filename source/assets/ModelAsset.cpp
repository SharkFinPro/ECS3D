#include "ModelAsset.h"
#include "AssetManager.h"
#include "../ECS3D.h"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/assets/AssetManager.h>

ModelAsset::ModelAsset(const uuids::uuid uuid,
                       std::string path)
  : Asset(uuid, path.substr(path.find_last_of('/') + 1)), m_path(std::move(path))
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
  m_model = m_assetManager->getECS()->getRenderer()->getAssetManager()->loadModel(m_path.c_str());
}

void ModelAsset::displayGui()
{
  Asset::displayGui();

  ImGui::Button("Model", {150, 150});
}

nlohmann::json ModelAsset::serialize()
{
  const nlohmann::json data = {
    { "name", m_path },
    { "filePath", m_path },
    { "uuid", uuids::to_string(m_uuid) }
  };

  return data;
}
