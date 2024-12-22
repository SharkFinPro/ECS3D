#include "Asset.h"
#include <imgui.h>

constexpr int MAX_CHARACTERS = 30;

Asset::Asset(std::string name)
  : assetManager(nullptr), name(name)
{
  name.resize(MAX_CHARACTERS);
}

void Asset::setManager(AssetManager* assetManager)
{
  this->assetManager = assetManager;
}

std::string Asset::getName() const
{
  return name;
}

void Asset::displayGui()
{
  ImGui::SeparatorText(name.c_str());
}
