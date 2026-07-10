#include "AssetInspector.h"
#include "AssetDisplay.h"
#include "GuiComponents.h"
#include <assets/AssetRegistry.h>
#include <imgui.h>
#include <string>
#include <utility>

AssetInspector::AssetInspector(std::shared_ptr<GpuAssetCache> assetCache)
  : m_assetCache(std::move(assetCache))
{}

void AssetInspector::displayTypeChip(const AssetRecord& record) const
{
  const char* label = assetDisplay::typeLabel(record.type);
  ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - gc::iconPillWidth(label));
  gc::iconPill(assetDisplay::icon(record.type), label, assetDisplay::color(record.type));
}

void AssetInspector::display(const AssetRecord& record)
{
  displayHeader(record);
}

void AssetInspector::displayHeader(const AssetRecord& record) const
{
  // Display name (primary text), then metadata rows shared by every asset type.
  ImGui::TextColored(theme::t1, "%s", assetDisplay::name(record).c_str());

  ImGui::Spacing();

  gc::rowLabel("UUID");
  ImGui::TextColored(theme::t3, "%s", uuids::to_string(record.uuid).c_str());

  // Scenes and prefabs have no file on disk (their `path` holds the display name), so only the file-backed
  // types show a source path.
  if (record.type != AssetType::Scene && record.type != AssetType::Prefab && !record.path.empty())
  {
    gc::rowLabel("Path");
    ImGui::TextColored(theme::t2, "%s", record.path.c_str());
  }
}
