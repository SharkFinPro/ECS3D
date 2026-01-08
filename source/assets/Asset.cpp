#include "Asset.h"
#include <imgui.h>

constexpr int MAX_CHARACTERS = 30;

Asset::Asset(std::string name)
  : m_name(std::move(name))
{
  name.resize(MAX_CHARACTERS);
}

void Asset::setManager(AssetManager* assetManager)
{
  m_assetManager = assetManager;
}

std::string Asset::getName() const
{
  return m_name;
}

void Asset::displayGui()
{
  ImGui::Text(m_name.substr(m_name.find_last_of('/') + 1).c_str());
}
