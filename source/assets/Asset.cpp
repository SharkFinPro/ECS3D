#include "Asset.h"
#include <imgui.h>

Asset::Asset(const AssetType assetType,
             const uuids::uuid uuid,
             std::string name)
  : m_assetType(assetType), m_uuid(uuid), m_name(std::move(name))
{}

void Asset::setManager(AssetManager* assetManager)
{
  m_assetManager = assetManager;
}

AssetType Asset::getAssetType() const
{
  return m_assetType;
}

std::string Asset::getName() const
{
  return m_name;
}

void Asset::displayGui([[maybe_unused]] const float cellSize)
{
  ImGui::Text(m_name.c_str());
}

uuids::uuid Asset::getUUID() const
{
  return m_uuid;
}
