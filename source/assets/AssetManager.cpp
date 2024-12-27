#include "AssetManager.h"
#include "Asset.h"
#include <imgui.h>

AssetManager::AssetManager(ECS3D* ecs)
  : ecs(ecs)
{}

ECS3D* AssetManager::getECS() const
{
  return ecs;
}

void AssetManager::displayGui()
{
  ImGui::Begin("Assets");

  constexpr int cellSize = 200;
  const int width = static_cast<int>(ImGui::GetContentRegionAvail().x);

  ImGui::Columns(std::max(1, width / cellSize), 0, false);

  for (const auto& [name, asset] : assets)
  {
    ImGui::PushID(&asset);

    asset->displayGui();

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
      ImGui::SetDragDropPayload("asset", &asset, sizeof(asset));
      ImGui::Text(name.c_str());
      ImGui::EndDragDropSource();
    }

    ImGui::PopID();

    ImGui::NextColumn();
  }

  ImGui::End();
}
