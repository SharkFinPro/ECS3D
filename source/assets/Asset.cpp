#include "Asset.h"
#include <imgui.h>

Asset::Asset(std::string name)
  : assetManager(nullptr), name(name)
{
  name.resize(MAX_CHARACTERS);
}

void Asset::setManager(AssetManager* assetManager)
{
  this->assetManager = assetManager;
}

void Asset::displayGui()
{
  ImGui::Text(name.c_str());
}
