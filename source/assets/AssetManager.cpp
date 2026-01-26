#include "AssetManager.h"
#include "Asset.h"
#include "ModelAsset.h"
#include "TextureAsset.h"
#include <imgui.h>
#include <nlohmann/json.hpp>

AssetManager::AssetManager(ECS3D* ecs)
  : m_ecs(ecs),
    m_rng([] {
      std::random_device rd;
      auto seed_data = std::array<int, std::mt19937::state_size>{};
      std::ranges::generate(seed_data, std::ref(rd));
      std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
      return std::mt19937(seq);
    }()),
    m_uuidGenerator(m_rng)
{}

ECS3D* AssetManager::getECS() const
{
  return m_ecs;
}

void AssetManager::displayGui()
{
  ImGui::Begin("Assets");

  constexpr int cellSize = 200;
  const int width = static_cast<int>(ImGui::GetContentRegionAvail().x);

  ImGui::Columns(std::max(1, width / cellSize), 0, false);

  for (const auto& [name, asset] : m_assets)
  {
    ImGui::PushID(&asset);

    asset->displayGui();

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
      ImGui::SetDragDropPayload("asset", &asset, sizeof(asset));
      ImGui::Text(asset->getName().c_str());
      ImGui::EndDragDropSource();
    }

    ImGui::PopID();

    ImGui::NextColumn();
  }

  ImGui::End();
}

nlohmann::json AssetManager::serialize()
{
  nlohmann::json data = {
    { "models", nlohmann::json::array() },
    { "textures", nlohmann::json::array() }
  };

  for (const auto& [_, asset] : m_assets)
  {
    if (const auto textureAsset = std::dynamic_pointer_cast<TextureAsset>(asset))
    {
      data["textures"].push_back(textureAsset->serialize());
    }
    else if (const auto modelAsset = std::dynamic_pointer_cast<ModelAsset>(asset))
    {
      data["models"].push_back(modelAsset->serialize());
    }
  }

  return data;
}
