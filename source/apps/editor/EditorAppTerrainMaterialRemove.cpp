#include "EditorApp.h"
#include <terrain/TerrainMaterialRemap.h>
#include <terrain/TerrainPalette.h>
#include <terrain/TerrainStore.h>
#include <terrain/TerrainVolume.h>
#include <edits/RecordTerrainEdits.h>
#include <imgui.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
  constexpr const char* removePopupId = "Remove material";

  [[nodiscard]] const std::vector<terrain::TerrainMaterial>& livePalette(const terrain::TerrainStore& store,
                                                                         const uuids::uuid& terrainUuid)
  {
    static const std::vector<terrain::TerrainMaterial> none;
    const terrain::TerrainDescription* description = store.description(terrainUuid);
    return description ? description->palette : none;
  }
}

std::optional<std::string> EditorApp::sendTerrainRemap(const uuids::uuid& terrainUuid,
                                                       const TerrainPaletteEditor::Result& result)
{
  if (auto refusal = TerrainPaletteEditor::remapRefusal(terrainPaletteContext(terrainUuid)))
  {
    return refusal;
  }

  auto prediction = edits::predictTerrainRemap(terrainUuid, *m_terrainStore, *result.remap, result.palette);
  if (!prediction)
  {
    return "That change leaves the materials as they are.";
  }

  sendTerrainEdit(prediction->message);
  m_playHistory.current().record(std::move(prediction->command));
  noteRecordedEdit();

  return std::nullopt;
}

void EditorApp::requestTerrainMaterialRemove(const uuids::uuid& terrainUuid, const size_t index,
                                             const TerrainPaletteRun& run)
{
  if (auto refusal = TerrainPaletteEditor::remapRefusal(terrainPaletteContext(terrainUuid)))
  {
    m_terrainPaletteNote.raise(terrainUuid, index, std::move(*refusal));
    return;
  }

  const auto& live = livePalette(*m_terrainStore, terrainUuid);
  const auto volume = m_terrainStore->find(terrainUuid);

  if (TerrainPaletteEditor::removeNeedsChoice(live.size(), index, terrain::materialsInUse(*volume)))
  {
    m_terrainRemovePrompt.open(terrainUuid, live, index);
    return;
  }

  run(TerrainPaletteEditor::remove(live, index, TerrainPaletteEditor::defaultReplacement(index)),
      { TextureModeRows::Change::Kind::remove, index, index });
}

void EditorApp::confirmTerrainMaterialRemove(const uuids::uuid& terrainUuid)
{
  const size_t index = m_terrainRemovePrompt.index();
  const size_t replacement = m_terrainRemovePrompt.replacement();
  m_terrainRemovePrompt.close();

  finishTerrainPaletteEdit(terrainUuid, index,
                           TerrainPaletteEditor::remove(livePalette(*m_terrainStore, terrainUuid), index, replacement),
                           { TextureModeRows::Change::Kind::remove, index, index });
}

void EditorApp::drawTerrainRemovePrompt(const uuids::uuid& terrainUuid)
{
  const auto& live = livePalette(*m_terrainStore, terrainUuid);
  const bool open = m_terrainRemovePrompt.matches(terrainUuid, live);
  m_terrainRemovePrompt.markDrawn();

  if (open && !ImGui::IsPopupOpen(removePopupId))
  {
    ImGui::OpenPopup(removePopupId);
  }

  if (!ImGui::BeginPopupModal(removePopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    return;
  }

  if (open)
  {
    drawTerrainRemovePromptBody(terrainUuid, live);
  }

  if (!m_terrainRemovePrompt.isOpen())
  {
    ImGui::CloseCurrentPopup();
  }

  ImGui::EndPopup();
}

void EditorApp::drawTerrainRemovePromptBody(const uuids::uuid& terrainUuid,
                                            const std::vector<terrain::TerrainMaterial>& palette)
{
  const auto removed = static_cast<uint8_t>(m_terrainRemovePrompt.index());
  ImGui::Text("%s is painted on the terrain.", materialLabel(palette, removed).c_str());
  ImGui::TextUnformatted("Painted ground becomes:");
  drawTerrainRemoveChoices(palette);
  ImGui::Spacing();

  if (ImGui::Button("Remove"))
  {
    confirmTerrainMaterialRemove(terrainUuid);
  }

  ImGui::SameLine();

  if (ImGui::Button("Cancel"))
  {
    m_terrainRemovePrompt.close();
  }
}

void EditorApp::drawTerrainRemoveChoices(const std::vector<terrain::TerrainMaterial>& palette)
{
  const float side = ImGui::GetTextLineHeight();

  for (size_t index = 0; index < palette.size(); ++index)
  {
    if (index == m_terrainRemovePrompt.index())
    {
      continue;
    }

    const auto material = static_cast<uint8_t>(index);
    const std::string label = materialLabel(palette, material);
    ImGui::PushID(static_cast<int>(index));

    bool clicked = drawTerrainMaterialSwatch(materialSwatch(palette, material), side, false, label);
    ImGui::SameLine();
    clicked |= ImGui::Selectable((label + "###choice").c_str(), index == m_terrainRemovePrompt.replacement(),
                                 ImGuiSelectableFlags_NoAutoClosePopups);

    if (clicked)
    {
      m_terrainRemovePrompt.choose(index);
    }

    ImGui::PopID();
  }
}
