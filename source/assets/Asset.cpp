#include "Asset.h"
#include <imgui.h>

constexpr int MAX_CHARACTERS = 30;

Asset::Asset(const uuids::uuid uuid,
             std::string name)
  : m_uuid(uuid), m_name(std::move(name))
{
  m_name.resize(MAX_CHARACTERS);
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
  ImGui::Text(m_name.c_str());
}
