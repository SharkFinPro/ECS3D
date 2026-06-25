#include "AssetBrowserPanel.h"
#include "AssetDragDrop.h"
#include <GpuAssetCache.h>
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/window/Window.h>
#include <VulkanEngine/components/assets/textures/Texture2D.h>
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

AssetBrowserPanel::AssetBrowserPanel(const AssetRegistry* assetRegistry, std::shared_ptr<GpuAssetCache> assetCache)
  : m_assetRegistry(assetRegistry),
    m_assetCache(std::move(assetCache))
{}

void AssetBrowserPanel::setLoadSceneCallback(LoadSceneCallback callback)
{
  m_onLoadScene = std::move(callback);
}

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

std::string AssetBrowserPanel::displayName(const AssetRecord& record)
{
  switch (record.type)
  {
    case AssetType::Scene: return record.path;        // scenes store their display name in path
    case AssetType::Script: return record.className;
    default: return std::filesystem::path(record.path).filename().string();
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

  // Grid: size the tiles by the window content scale and pack as many columns as fit.
  const float contentScale = m_assetCache->getRenderer()->getWindow()->getContentScale();
  const float cellSize = 72.0f * contentScale;
  const float available = ImGui::GetContentRegionAvail().x;
  const int columns = std::max(1, static_cast<int>(available / (cellSize + 16.0f)));

  ImGui::Columns(columns, nullptr, false);

  for (const auto& [uuid, record] : m_assetRegistry->getAssets())
  {
    if (m_filter != AssetType::Unknown && record.type != m_filter)
    {
      continue;
    }

    const std::string name = displayName(record);

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

    displayAsset(uuid, record, cellSize, name);

    ImGui::PopID();

    ImGui::NextColumn();
  }

  ImGui::Columns(1);

  ImGui::End();
}

void AssetBrowserPanel::displayAsset(const uuids::uuid& uuid, const AssetRecord& record, const float cellSize, const std::string& name)
{
  ImGui::TextWrapped("%s", name.c_str());

  bool drewThumbnail = false;

  // Textures show their actual image; everything else gets a labelled tile.
  if (record.type == AssetType::Texture)
  {
    try
    {
      if (const auto texture = m_assetCache->getTexture(uuid))
      {
        ImGui::ImageButton("##thumb", texture->getImGuiTexture(), { cellSize, cellSize });
        drewThumbnail = true;
      }
    }
    catch (const std::exception&)
    {
      // fall back to a labelled tile below
    }
  }

  if (!drewThumbnail)
  {
    ImGui::Button(assetTypeLabel(record.type), { cellSize, cellSize });
  }

  // Double-click a scene tile to make it the active scene.
  if (record.type == AssetType::Scene && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
  {
    if (m_onLoadScene)
    {
      m_onLoadScene(uuid);
    }
  }

  // Drag source (model/texture/script), tagged by type so only the right drop target accepts it.
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
