#include "AssetManager.h"
#include "Asset.h"
#include <imgui.h>

void AssetManager::setECS(ECS3D* ecs)
{
  this->ecs = ecs;
}

ECS3D * AssetManager::getECS() const
{
  return ecs;
}

void AssetManager::displayGui()
{
  ImGui::Begin("Assets");

  for (const auto& asset : assets)
  {
    asset->displayGui();
  }

  ImGui::End();
}
