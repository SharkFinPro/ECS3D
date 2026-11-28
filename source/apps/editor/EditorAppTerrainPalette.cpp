#include "EditorApp.h"
#include <assets/AssetRegistry.h>
#include <objects/components/collisions/Terrain.h>
#include <terrain/TerrainAddition.h>
#include <terrain/TerrainStore.h>
#include <terrain/TerrainVolume.h>
#include <edits/RecordTerrainEdits.h>
#include <components/AssetSlot.h>
#include <AssetDragDrop.h>
#include <EditorTheme.h>
#include <GuiComponents.h>
#include <NetClient.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <glm/vec3.hpp>
#include <bitset>
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
  constexpr size_t nameBufferSize = 256;

  [[nodiscard]] const std::vector<terrain::TerrainMaterial>& paletteOf(const terrain::TerrainStore& store,
                                                                       const uuids::uuid& terrainUuid)
  {
    static const std::vector<terrain::TerrainMaterial> none;
    const terrain::TerrainDescription* description = store.description(terrainUuid);
    return description ? description->palette : none;
  }

  [[nodiscard]] MaterialNameField::Report drawNameField(std::string& name)
  {
    char buffer[nameBufferSize] = {};
    name.copy(buffer, sizeof(buffer) - 1);

    MaterialNameField::Report report;
    ImGui::SetNextItemWidth(-FLT_MIN);
    report.changed = ImGui::InputText("##name", buffer, sizeof(buffer));
    report.settled = ImGui::IsItemDeactivatedAfterEdit() || !ImGui::IsItemActive();

    if (report.changed)
    {
      name = buffer;
    }

    return report;
  }
}

TerrainPaletteEditor::Context EditorApp::terrainPaletteContext(const uuids::uuid& terrainUuid) const
{
  return { m_serverEditable, m_sceneStatus == SceneStatus::stopped,
           m_terrainStore->description(terrainUuid) != nullptr };
}

bool EditorApp::applyTerrainPalette(const uuids::uuid& terrainUuid, std::vector<terrain::TerrainMaterial> palette)
{
  auto command = edits::commandForTerrainPalette(terrainUuid, *m_terrainStore, palette);
  if (!command)
  {
    return false;
  }

  auto description = *m_terrainStore->description(terrainUuid);
  description.palette = std::move(palette);
  const auto asset = terrain::addTerrainAssetJson(description);

  if (!replication::applyAddAssetWithTerrain(*m_assetRegistry, *m_sceneManager, m_componentRegistry, *m_terrainStore,
                                             asset, false))
  {
    return false;
  }

  m_netClient->send(replication::packAddAsset(asset));
  m_playHistory.current().record(std::move(*command));
  noteRecordedEdit();

  return true;
}

bool EditorApp::finishTerrainPaletteEdit(const uuids::uuid& terrainUuid, TerrainPaletteEditor::Result result,
                                         const TextureModeRows::Change& change)
{
  if (!result.ok())
  {
    m_terrainPaletteMessage = *result.refusal;
    return false;
  }

  m_terrainPaletteMessage.clear();

  const auto after = result.palette;
  if (!applyTerrainPalette(terrainUuid, std::move(result.palette)))
  {
    return false;
  }

  m_terrainTextureModes.follow(change, after);

  return true;
}

void EditorApp::flushTerrainMaterialName()
{
  const auto canSend = m_serverEditable && m_sceneStatus == SceneStatus::stopped;

  const auto materialAt = [this](const uuids::uuid& terrainUuid, const size_t index)
    -> std::optional<terrain::TerrainMaterial> {
    const auto& palette = paletteOf(*m_terrainStore, terrainUuid);
    return index < palette.size() ? std::optional<terrain::TerrainMaterial>(palette[index]) : std::nullopt;
  };

  const auto colorCommit = m_terrainMaterialColor.endFrame(canSend, materialAt);

  if (colorCommit)
  {
    finishTerrainPaletteEdit(colorCommit->terrain, TerrainPaletteEditor::setColor(
      paletteOf(*m_terrainStore, colorCommit->terrain), colorCommit->index, colorCommit->value));
  }

  const auto commit = m_terrainMaterialName.endFrame(canSend, materialAt);

  if (commit)
  {
    finishTerrainPaletteEdit(commit->terrain, TerrainPaletteEditor::rename(
      paletteOf(*m_terrainStore, commit->terrain), commit->index, commit->value));
  }
}

void EditorApp::drawTerrainMaterials(const std::shared_ptr<Terrain>& component)
{
  if (!component || component != selectedTerrain())
  {
    return;
  }

  const uuids::uuid terrainUuid = component->getTerrainUUID();
  const auto palette = paletteOf(*m_terrainStore, terrainUuid);
  const auto refusal = TerrainPaletteEditor::editRefusal(terrainPaletteContext(terrainUuid));

  m_terrainTextureModes.sync(terrainUuid, palette);

  ImGui::Spacing();
  ImGui::TextUnformatted("Materials");

  if (palette.empty())
  {
    ImGui::TextColored(theme::t2, "No palette: the default colors are used.");
  }

  ImGui::PushID("terrainMaterials");

  const bool edited = TerrainPaletteEditor::forEachRowUntilEdit(palette.size(), [&](const size_t index) {
    return drawTerrainMaterialRow(terrainUuid, index, palette, refusal);
  });

  drawTerrainAddMaterial(terrainUuid, palette.size(), refusal, edited);

  ImGui::PopID();

  drawTerrainPaletteNotes(refusal);
}

void EditorApp::drawTerrainAddMaterial(const uuids::uuid& terrainUuid, const size_t paletteSize,
                                       const std::optional<std::string>& refusal, const bool edited)
{
  const bool full = paletteSize >= terrain::maxPaletteSize;

  ImGui::BeginDisabled(refusal.has_value() || full);
  if (ImGui::Button("Add material") && !edited)
  {
    finishTerrainPaletteEdit(terrainUuid, TerrainPaletteEditor::add(paletteOf(*m_terrainStore, terrainUuid)));
  }
  ImGui::EndDisabled();

  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && (refusal || full))
  {
    ImGui::SetTooltip("%s", refusal ? refusal->c_str() : "A terrain holds at most 16 materials.");
  }
}

void EditorApp::drawTerrainPaletteNotes(const std::optional<std::string>& refusal) const
{
  if (refusal)
  {
    ImGui::TextColored(theme::t2, "%s", refusal->c_str());
  }

  if (!m_terrainPaletteMessage.empty())
  {
    ImGui::TextColored(theme::t2, "%s", m_terrainPaletteMessage.c_str());
  }
}

bool EditorApp::drawTerrainMaterialRow(const uuids::uuid& terrainUuid, const size_t index,
                                       const std::vector<terrain::TerrainMaterial>& palette,
                                       const std::optional<std::string>& editRefusal)
{
  const terrain::TerrainMaterial& material = palette[index];
  const bool canEdit = !editRefusal.has_value();
  bool edited = false;

  // Every intent is built from the palette the store holds now, not from this frame's copy, and none runs once an
  // edit has applied this frame.
  const TerrainPaletteRun run = [&](TerrainPaletteEditor::Result result, const TextureModeRows::Change& change) {
    if (!edited)
    {
      edited = finishTerrainPaletteEdit(terrainUuid, std::move(result), change);
    }
  };

  ImGui::PushID(static_cast<int>(index));

  const bool active = clampMaterial(m_terrainTool.activeMaterial(), palette.size()) == index;
  if (ImGui::RadioButton("##use", active) && !active)
  {
    m_terrainTool.setActiveMaterial(static_cast<uint8_t>(index));
  }

  if (ImGui::IsItemHovered())
  {
    ImGui::SetTooltip("%s", "Paint with this material.");
  }

  ImGui::SameLine();

  ImGui::BeginDisabled(!canEdit);

  if (const auto commit = m_terrainMaterialName.frame(terrainUuid, index, material, canEdit, [](std::string& name) {
        return drawNameField(name);
      }))
  {
    run(TerrainPaletteEditor::rename(paletteOf(*m_terrainStore, terrainUuid), commit->index, commit->value), {});
  }

  drawTerrainMaterialOrder(terrainUuid, index, palette.size(), run);
  drawTerrainMaterialSurface(terrainUuid, index, material, canEdit, run);

  ImGui::EndDisabled();

  ImGui::PopID();
  ImGui::Separator();

  return edited;
}

void EditorApp::drawTerrainMaterialOrder(const uuids::uuid& terrainUuid, const size_t index, const size_t paletteSize,
                                         const TerrainPaletteRun& run)
{
  const auto& live = paletteOf(*m_terrainStore, terrainUuid);

  ImGui::BeginDisabled(index == 0);
  if (ImGui::SmallButton("Up"))
  {
    run(TerrainPaletteEditor::move(live, index, index - 1), { TextureModeRows::Change::Kind::move, index, index - 1 });
  }
  ImGui::EndDisabled();

  ImGui::SameLine();

  ImGui::BeginDisabled(index + 1 >= paletteSize);
  if (ImGui::SmallButton("Down"))
  {
    run(TerrainPaletteEditor::move(live, index, index + 1), { TextureModeRows::Change::Kind::move, index, index + 1 });
  }
  ImGui::EndDisabled();

  ImGui::SameLine();

  if (ImGui::SmallButton("Remove"))
  {
    run(TerrainPaletteEditor::remove(live, index, [this, &terrainUuid] {
          const auto volume = m_terrainStore->find(terrainUuid);
          return volume ? terrain::materialsInUse(*volume) : std::bitset<256>{};
        }),
        { TextureModeRows::Change::Kind::remove, index, index });
  }
}

void EditorApp::drawTerrainMaterialSurface(const uuids::uuid& terrainUuid, const size_t index,
                                           const terrain::TerrainMaterial& material, const bool canEdit,
                                           const TerrainPaletteRun& run)
{
  const bool textureMode = !TerrainPaletteEditor::isColorMode(material) || m_terrainTextureModes.contains(index);

  if (ImGui::RadioButton("Color", !textureMode) && textureMode)
  {
    m_terrainTextureModes.erase(index);
    run(TerrainPaletteEditor::useColor(paletteOf(*m_terrainStore, terrainUuid), index), {});
  }

  ImGui::SameLine();

  if (ImGui::RadioButton("Texture", textureMode) && !textureMode)
  {
    m_terrainTextureModes.insert(index);
  }

  uuids::uuid dropped;

  if (textureMode)
  {
    const AssetSlotStyle albedoSlot{ "albedoSlot", "Albedo", assetDragDrop::texture, true,
                                     gc::SecIcon::image, theme::accent };
    if (drawAssetSlot(m_assetCache, m_assetRegistry.get(), albedoSlot, material.albedoTexture, dropped))
    {
      run(TerrainPaletteEditor::setAlbedo(paletteOf(*m_terrainStore, terrainUuid), index, dropped), {});
    }
  }
  else
  {
    drawTerrainMaterialColor(terrainUuid, index, material, canEdit, run);
  }

  const AssetSlotStyle specularSlot{ "specularSlot", "Specular", assetDragDrop::texture, true,
                                     gc::SecIcon::image, theme::accent };
  if (drawAssetSlot(m_assetCache, m_assetRegistry.get(), specularSlot, material.specularTexture, dropped))
  {
    run(TerrainPaletteEditor::setSpecular(paletteOf(*m_terrainStore, terrainUuid), index, dropped), {});
  }
}

void EditorApp::drawTerrainMaterialColor(const uuids::uuid& terrainUuid, const size_t index,
                                         const terrain::TerrainMaterial& material, const bool canEdit,
                                         const TerrainPaletteRun& run)
{
  const auto commit = m_terrainMaterialColor.frame(terrainUuid, index, material, canEdit, [this](glm::vec3& color) {
    MaterialColorField::Report report;
    report.changed = ImGui::ColorEdit3("##color", &color.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

    // Only the picker's own widget holds the edit open, not any other active item.
    const ImGuiID activeId = ImGui::GetActiveID();
    if (report.changed && activeId != 0)
    {
      m_terrainColorWidget = activeId;
    }

    report.settled = activeId == 0 || activeId != m_terrainColorWidget;
    return report;
  });

  if (commit)
  {
    run(TerrainPaletteEditor::setColor(paletteOf(*m_terrainStore, terrainUuid), commit->index, commit->value), {});
  }
}
