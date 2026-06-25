#include "AssetBrowserPanel.h"
#include "AssetDragDrop.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

AssetBrowserPanel::AssetBrowserPanel(const AssetRegistry* assetRegistry)
  : m_assetRegistry(assetRegistry)
{}

const char* AssetBrowserPanel::assetTypeLabel(const AssetType type)
{
  switch (type)
  {
    case AssetType::Model: return "Model";
    case AssetType::Texture: return "Texture";
    case AssetType::Scene: return "Scene";
    case AssetType::Script: return "Script";
    default: return "All";
  }
}

void AssetBrowserPanel::displayGui()
{
  ImGui::Begin("Assets");

  // Type filter.
  if (ImGui::BeginCombo("Type", assetTypeLabel(m_filter)))
  {
    for (const auto type : { AssetType::Unknown, AssetType::Model, AssetType::Texture, AssetType::Scene, AssetType::Script })
    {
      if (ImGui::Selectable(assetTypeLabel(type), m_filter == type))
      {
        m_filter = type;
      }
    }

    ImGui::EndCombo();
  }

  ImGui::InputText("Search", m_search, sizeof(m_search));

  ImGui::Separator();

  std::string query = m_search;
  std::ranges::transform(query, query.begin(), [](const unsigned char c) { return std::tolower(c); });

  for (const auto& [uuid, record] : m_assetRegistry->getAssets())
  {
    if (m_filter != AssetType::Unknown && record.type != m_filter)
    {
      continue;
    }

    const std::string name = record.type == AssetType::Script
      ? record.className
      : std::filesystem::path(record.path).filename().string();

    if (!query.empty())
    {
      std::string lowered = name;
      std::ranges::transform(lowered, lowered.begin(), [](const unsigned char c) { return std::tolower(c); });

      if (lowered.find(query) == std::string::npos)
      {
        continue;
      }
    }

    ImGui::PushID(uuids::to_string(uuid).c_str());

    ImGui::Selectable(name.c_str());

    // Drag source: the payload is the asset uuid string, tagged by type so only the right slot accepts it.
    if (const char* payloadId = assetDragDrop::payloadId(record.type))
    {
      if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
      {
        const std::string uuidStr = uuids::to_string(uuid);
        ImGui::SetDragDropPayload(payloadId, uuidStr.c_str(), uuidStr.size());
        ImGui::TextUnformatted(name.c_str());
        ImGui::EndDragDropSource();
      }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", assetTypeLabel(record.type));

    ImGui::PopID();
  }

  ImGui::End();
}

void AssetBrowserPanel::displayMenuWidget()
{
  if (ImGui::BeginMenu("Assets"))
  {
    // TODO: migrate AssetManager::displayMenuWidget / displayCreateAssetPopup — importing a model/
    // TODO:   texture/script needs an nfd file picker AND a server-side asset-add command (the asset
    // TODO:   list is authoritative on the server), so these are disabled until that path exists.
    ImGui::BeginDisabled();
    ImGui::MenuItem("Import Model...");
    ImGui::MenuItem("Import Texture...");
    ImGui::MenuItem("Import Script...");
    ImGui::EndDisabled();

    ImGui::EndMenu();
  }
}
