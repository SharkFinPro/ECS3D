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
  const auto* description = m_terrainStore->description(terrainUuid);
  const auto volume = m_terrainStore->find(terrainUuid);
  const bool complete = description && volume && volume->chunks().size() >= description->chunkCount;
  const bool strokeBusy = m_terrainBrush.active() || m_terrainBrush.awaitingFinalChunks();

  return { m_serverEditable, m_sceneStatus == SceneStatus::stopped, description != nullptr, complete, strokeBusy };
}

std::optional<std::string> EditorApp::applyTerrainPalette(const uuids::uuid& terrainUuid,
                                                          std::vector<terrain::TerrainMaterial> palette)
{
  auto command = edits::commandForTerrainPalette(terrainUuid, *m_terrainStore, palette);
  if (!command)
  {
    return "That change leaves the materials as they are.";
  }

  auto description = *m_terrainStore->description(terrainUuid);
  description.palette = std::move(palette);
  const auto asset = terrain::addTerrainAssetJson(description);

  if (!replication::applyAddAssetWithTerrain(*m_assetRegistry, *m_sceneManager, m_componentRegistry, *m_terrainStore,
                                             asset, false))
  {
    return "The editor could not apply the new materials.";
  }

  m_netClient->send(replication::packAddAsset(asset));
  m_playHistory.current().record(std::move(*command));
  noteRecordedEdit();

  return std::nullopt;
}

bool EditorApp::finishTerrainPaletteEdit(const uuids::uuid& terrainUuid, const size_t noteIndex,
                                         TerrainPaletteEditor::Result result, const TextureModeRows::Change& change)
{
  const size_t selected = clampMaterial(m_terrainTool.activeMaterial(), paletteOf(*m_terrainStore, terrainUuid).size());

  if (!result.refusal)
  {
    // Another view may draw the new palette over the old indices until the remapped chunks arrive; the server sends
    // the chunks first, so that lasts at most as long as the two messages take.
    result.refusal = result.remap ? sendTerrainRemap(terrainUuid, result)
                                  : applyTerrainPalette(terrainUuid, result.palette);
  }

  if (result.refusal)
  {
    m_terrainPaletteNote.raise(terrainUuid, noteIndex, std::move(*result.refusal));
    return false;
  }

  m_terrainPaletteNote.clear();
  m_terrainTextureModes.follow(change, result.palette);

  const auto shown = selectedTerrain();
  if (const auto follow = selectionToFollow(change, terrainUuid, shown ? shown->getTerrainUUID() : uuids::uuid{},
                                            selected, result.palette.size()))
  {
    m_terrainTool.setActiveMaterial(static_cast<uint8_t>(*follow));
  }

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

  m_terrainRemovePrompt.endFrame();

  if (const auto commit = m_terrainMaterialColor.endFrame(canSend, materialAt))
  {
    finishTerrainPaletteEdit(commit->terrain, commit->index, TerrainPaletteEditor::setColor(
      paletteOf(*m_terrainStore, commit->terrain), commit->index, commit->value));
  }

  if (const auto commit = m_terrainMaterialName.endFrame(canSend, materialAt))
  {
    finishTerrainPaletteEdit(commit->terrain, commit->index, TerrainPaletteEditor::rename(
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
  ImGui::PushID("terrainMaterials");

  if (ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen))
  {
    if (palette.empty())
    {
      ImGui::TextColored(theme::t2, "No palette: the default colors are used.");
    }
    else
    {
      drawTerrainMaterialList(palette);
    }

    const size_t selected = clampMaterial(m_terrainTool.activeMaterial(), palette.size());

    // The details are skipped the frame Add applies, so a pending name or color settles next frame through endFrame
    // against the palette the add left, instead of being dropped as this frame's second edit.
    if (!drawTerrainAddMaterial(terrainUuid, palette.size(), refusal) && !palette.empty())
    {
      drawTerrainMaterialDetails(terrainUuid, selected, palette, refusal);
    }

    if (refusal)
    {
      ImGui::TextColored(theme::t2, "%s", refusal->c_str());
    }
  }

  drawTerrainRemovePrompt(terrainUuid);

  ImGui::PopID();
}

void EditorApp::drawTerrainMaterialList(const std::vector<terrain::TerrainMaterial>& palette)
{
  const ImGuiStyle& style = ImGui::GetStyle();
  const float side = ImGui::GetTextLineHeight();
  const float height = TerrainPaletteEditor::listHeight(palette.size(), side + style.ItemSpacing.y,
                                                        style.WindowPadding.y * 2.0f - style.ItemSpacing.y);
  const uint8_t selected = clampMaterial(m_terrainTool.activeMaterial(), palette.size());

  ImGui::BeginChild("materialList", ImVec2(0.0f, height), true);

  for (size_t index = 0; index < palette.size(); ++index)
  {
    const auto material = static_cast<uint8_t>(index);
    const std::string label = materialLabel(palette, material);
    ImGui::PushID(static_cast<int>(index));

    bool clicked = drawTerrainMaterialSwatch(materialSwatch(palette, material), side, false, label);
    ImGui::SameLine();
    clicked |= ImGui::Selectable((label + "###row").c_str(), material == selected);

    if (clicked)
    {
      m_terrainTool.setActiveMaterial(material);
    }

    ImGui::PopID();
  }

  ImGui::EndChild();
}

bool EditorApp::drawTerrainAddMaterial(const uuids::uuid& terrainUuid, const size_t paletteSize,
                                       const std::optional<std::string>& refusal)
{
  const bool full = paletteSize >= terrain::maxPaletteSize;
  bool added = false;

  ImGui::BeginDisabled(refusal.has_value() || full);
  if (ImGui::Button("Add material"))
  {
    const auto& live = paletteOf(*m_terrainStore, terrainUuid);
    added = finishTerrainPaletteEdit(terrainUuid, clampMaterial(m_terrainTool.activeMaterial(), live.size()),
                                     TerrainPaletteEditor::add(live));
  }
  ImGui::EndDisabled();

  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && (refusal || full))
  {
    ImGui::SetTooltip("%s", refusal ? refusal->c_str() : "A terrain holds at most 16 materials.");
  }

  if (added)
  {
    m_terrainTool.setActiveMaterial(static_cast<uint8_t>(paletteOf(*m_terrainStore, terrainUuid).size() - 1));
  }

  return added;
}

void EditorApp::drawTerrainMaterialDetails(const uuids::uuid& terrainUuid, const size_t index,
                                           const std::vector<terrain::TerrainMaterial>& palette,
                                           const std::optional<std::string>& editRefusal)
{
  const terrain::TerrainMaterial& material = palette[index];
  const bool canEdit = !editRefusal.has_value();
  bool applied = false;

  // Every intent is built from the palette the store holds now, not from this frame's copy, and none runs once an
  // edit has applied this frame.
  const TerrainPaletteRun run = [&](TerrainPaletteEditor::Result result, const TextureModeRows::Change& change) {
    if (!applied)
    {
      applied = finishTerrainPaletteEdit(terrainUuid, index, std::move(result), change);
    }
  };

  ImGui::Separator();
  ImGui::PushID(static_cast<int>(index));
  ImGui::BeginDisabled(!canEdit);

  if (const auto commit = m_terrainMaterialName.frame(terrainUuid, index, material, canEdit, [](std::string& name) {
        return drawNameField(name);
      }))
  {
    run(TerrainPaletteEditor::rename(paletteOf(*m_terrainStore, terrainUuid), commit->index, commit->value), {});
  }

  drawTerrainMaterialSurface(terrainUuid, index, material, canEdit, run);
  drawTerrainMaterialOrder(terrainUuid, index, palette.size(), run);

  ImGui::EndDisabled();
  ImGui::PopID();
}

void EditorApp::drawTerrainMaterialOrder(const uuids::uuid& terrainUuid, const size_t index, const size_t paletteSize,
                                         const TerrainPaletteRun& run)
{
  const auto& live = paletteOf(*m_terrainStore, terrainUuid);

  ImGui::BeginDisabled(index == 0);
  if (ImGui::SmallButton("Move up"))
  {
    run(TerrainPaletteEditor::move(live, index, index - 1),
        { TextureModeRows::Change::Kind::move, index, index - 1 });
  }
  ImGui::EndDisabled();

  ImGui::SameLine();

  ImGui::BeginDisabled(index + 1 >= paletteSize);
  if (ImGui::SmallButton("Move down"))
  {
    run(TerrainPaletteEditor::move(live, index, index + 1),
        { TextureModeRows::Change::Kind::move, index, index + 1 });
  }
  ImGui::EndDisabled();

  ImGui::SameLine();

  if (ImGui::SmallButton("Remove"))
  {
    requestTerrainMaterialRemove(terrainUuid, index, run);
  }

  if (const std::string& note = m_terrainPaletteNote.textFor(terrainUuid, index); !note.empty())
  {
    ImGui::SameLine();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::t2, "%s", note.c_str());
    ImGui::PopTextWrapPos();
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

bool EditorApp::drawTerrainMaterialSwatch(const MaterialSwatch& swatch, const float side, const bool active,
                                          const std::string& tooltip)
{
  ImTextureID thumb = 0;
  if (!swatch.texture.is_nil())
  {
    thumb = textureThumbnail(m_assetCache, swatch.texture);
  }

  bool clicked = false;

  if (thumb != 0)
  {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    clicked = ImGui::ImageButton("##material", thumb, ImVec2(side, side));
    ImGui::PopStyleVar();
  }
  else
  {
    const glm::vec3& color = swatch.color;
    clicked = ImGui::ColorButton("##material", ImVec4(color.r, color.g, color.b, 1.0f),
                                 ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop |
                                   ImGuiColorEditFlags_NoAlpha,
                                 ImVec2(side, side));
  }

  if (active)
  {
    ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                        ImGui::ColorConvertFloat4ToU32(theme::accent), 2.0f, 0, 2.0f);
  }

  if (ImGui::IsItemHovered())
  {
    ImGui::SetTooltip("%s", tooltip.c_str());
  }

  return clicked;
}
