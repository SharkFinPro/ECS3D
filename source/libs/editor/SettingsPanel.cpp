#include "SettingsPanel.h"
#include "EditorTheme.h"
#include "GuiComponents.h"
#include "KeybindDispatcher.h"
#include "SettingsPanelLayout.h"
#include <EditorCameraSettings.h>
#include <EditorGizmoSettings.h>
#include <EditorViewportSettings.h>
#include <Keybinds.h>
#include <SettingsStore.h>
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/camera/Camera.h>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {
  constexpr const char* windowName = "Settings";
  constexpr const char* openKey = "panels.settings.open";
  constexpr float minNavWidth = 158.0f;

  constexpr const char* appearanceLabel = "Appearance";
  constexpr const char* viewportLabel = "Viewport";
  constexpr const char* keybindsLabel = "Keybinds";

  // Mirrors the label's left inset in gc::menuRow when an icon is drawn.
  constexpr float navLabelInset = 34.0f;

  constexpr const char* capturePrompt = "Press a key... Esc to cancel";
  constexpr const char* rebindLabel = "Rebind";
  constexpr const char* cancelLabel = "Cancel";
  constexpr const char* unbindLabel = "Unbind";
  constexpr const char* resetLabel = "Reset";

  [[nodiscard]] float textWidth(const char* text)
  {
    return ImGui::CalcTextSize(text).x;
  }

  [[nodiscard]] float navWidth()
  {
    const float widestLabel = std::max({ textWidth(appearanceLabel), textWidth(viewportLabel),
                                         textWidth(keybindsLabel) });

    return std::max(minNavWidth, navLabelInset * 2.0f + widestLabel + 2.0f * ImGui::GetStyle().WindowPadding.x);
  }

  [[nodiscard]] float bindingColumnWidth()
  {
    return textWidth(capturePrompt);
  }

  [[nodiscard]] float actionsColumnWidth()
  {
    const ImGuiStyle& style = ImGui::GetStyle();
    const std::array<float, 3> labelWidths = {
      std::max(textWidth(rebindLabel), textWidth(cancelLabel)),
      textWidth(unbindLabel),
      textWidth(resetLabel)
    };

    return buttonRowWidth(labelWidths, style.FramePadding.x, style.ItemSpacing.x);
  }

  // The narrowest window that shows the nav and a Keybinds table with nothing clipped.
  [[nodiscard]] float settingsContentWidth()
  {
    const ImGuiStyle& style = ImGui::GetStyle();

    float widestAction = 0.0f;
    for (const auto& info : editorActions())
    {
      widestAction = std::max(widestAction, textWidth(info.label));
    }

    const float tableWidth = widestAction + bindingColumnWidth() + actionsColumnWidth() +
                             4.0f * style.CellPadding.x;

    return navWidth() + style.ItemSpacing.x + tableWidth + 2.0f * style.WindowPadding.x + style.ScrollbarSize;
  }

  std::string themeKey(const char* token)
  {
    return std::string("appearance.theme.") + token;
  }

  // A stored color is four components in 0..1. A wrong length or an out-of-range component is treated as
  // absent, the way the store treats a mistyped key, so a hand-edited file cannot leave the editor
  // unreadable. Testing the range rather than clamping to it is what catches an infinity parsed from
  // something like 1e400, which would otherwise reach ImGui as a color.
  void readColor(const SettingsStore& settings, const char* token, ImVec4& color)
  {
    const auto components = settings.get<std::vector<float>>(themeKey(token), {});
    if (components.size() != 4)
    {
      return;
    }

    for (const float component : components)
    {
      if (!(component >= 0.0f && component <= 1.0f))
      {
        return;
      }
    }

    color = { components[0], components[1], components[2], components[3] };
  }
}

SettingsPanel::SettingsPanel(SettingsStore& settings, std::shared_ptr<vke::VulkanEngine> renderer,
                             std::shared_ptr<KeybindTable> keybindTable,
                             std::shared_ptr<KeybindDispatcher> keybindDispatcher)
  : m_settings(&settings), m_renderer(std::move(renderer)), m_keybindTable(std::move(keybindTable)),
    m_keybindDispatcher(std::move(keybindDispatcher)), m_rebindFlow(*m_keybindTable, settings), m_open(settings.get<bool>(openKey, false))
{}

void SettingsPanel::applyStoredTheme(const SettingsStore& settings)
{
  for (const auto& token : theme::tokens())
  {
    readColor(settings, token.key, *token.value);
  }
}

void SettingsPanel::open()
{
  setOpen(true);

  m_focusRequested = true;
}

bool SettingsPanel::isOpen() const
{
  return m_open;
}

void SettingsPanel::setOpen(const bool open)
{
  m_open = open;
  m_settings->set(openKey, open);
}

void SettingsPanel::displayGui()
{
  if (!m_open)
  {
    return;
  }

  if (m_focusRequested)
  {
    ImGui::SetNextWindowFocus();
    m_focusRequested = false;
  }

  // FirstUseEver, so a layout saved in imgui.ini wins over this default.
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const auto defaultSize = settingsWindowDefaultSize(settingsContentWidth(), ImGui::GetFontSize(),
                                                     viewport->Size.x, viewport->Size.y);
  ImGui::SetNextWindowSize(ImVec2(defaultSize.x, defaultSize.y), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

  // Begin's own close box, rather than a button in the panel: the window is dockable and a docked tab
  // shows that box, so anything else would be a second way to do the same thing.
  bool stayOpen = true;

  // Begin draws the window's decorations but reports a collapsed or clipped one by returning false, so
  // the contents are skipped there. The close box still has to be honored either way, which is why End
  // and the check below sit outside.
  if (ImGui::Begin(windowName, &stayOpen))
  {
    displayNav();

    ImGui::SameLine();

    ImGui::BeginChild("SettingsContent", ImVec2(0.0f, 0.0f), false);

    switch (m_section)
    {
      case Section::appearance:
        displayAppearance();
        break;

      case Section::viewport:
        displayViewport();
        break;

      case Section::keybinds:
        displayKeybinds();
        break;
    }

    ImGui::EndChild();
  }

  ImGui::End();

  if (!stayOpen)
  {
    setOpen(false);
  }
}

void SettingsPanel::displayNav()
{
  // Always drawn, so the set of sections is legible without opening one. The child's border is what
  // separates it from the content pane.
  ImGui::BeginChild("SettingsNav", ImVec2(navWidth(), 0.0f), true);

  if (gc::menuRow(appearanceLabel, gc::SecIcon::image, 34.0f, m_section == Section::appearance))
  {
    m_section = Section::appearance;
  }

  if (gc::menuRow(viewportLabel, gc::SecIcon::pan, 34.0f, m_section == Section::viewport))
  {
    m_section = Section::viewport;
  }

  if (gc::menuRow(keybindsLabel, gc::SecIcon::block, 34.0f, m_section == Section::keybinds))
  {
    m_section = Section::keybinds;
  }

  ImGui::EndChild();
}

void SettingsPanel::displayAppearance()
{
  gc::sectionLabel("Appearance");

  ImGui::TextColored(theme::t2, "Editor theme colors. Changes apply as you make them.");

  ImGui::Spacing();

  if (ImGui::Button("Reset to defaults"))
  {
    for (const auto& token : theme::tokens())
    {
      *token.value = *token.defaultValue;

      // Cleared rather than written back: an absent key means "whatever this build ships", so a reset
      // keeps following the defaults if they change instead of pinning today's values forever.
      m_settings->clear(themeKey(token.key));
    }

    theme::applyStyle();
  }

  // A pass per group rather than a header whenever the value changes from the previous row: the order
  // of tokens() is then a readability choice rather than something the layout depends on.
  for (const auto& group : theme::groups())
  {
    ImGui::Spacing();
    gc::sectionLabel(group);

    for (const auto& token : theme::tokens())
    {
      if (std::strcmp(token.group, group) != 0)
      {
        continue;
      }

      // Keyed by the settings key rather than the label: two groups can carry the same label, and
      // ImGui would treat both rows as one widget.
      ImGui::PushID(token.key);

      if (ImGui::ColorEdit4(token.label, &token.value->x, ImGuiColorEditFlags_AlphaBar))
      {
        m_settings->set(themeKey(token.key), std::vector{ token.value->x, token.value->y,
                                                          token.value->z, token.value->w });

        // The tokens feed the global ImGuiStyle as well as the custom widgets, so the style has to be
        // rebuilt for a change to reach the plain ImGui controls.
        theme::applyStyle();
      }

      ImGui::PopID();
    }
  }
}

void SettingsPanel::displayViewport()
{
  gc::sectionLabel("Viewport");

  ImGui::TextColored(theme::t2, "Speed of the editor's free-fly camera - movement, mouse-look and "
                                 "scroll zoom together. Applies as you drag.");

  ImGui::Spacing();

  float speed = editorCameraSettings::readSpeed(*m_settings);
  if (gc::accentSlider("Camera Speed", &speed, editorCameraSettings::minSpeed, editorCameraSettings::maxSpeed))
  {
    editorCameraSettings::writeSpeed(*m_settings, speed);
    m_renderer->getCamera()->setSpeed(speed);
  }

  ImGui::Spacing();

  if (ImGui::Button("Reset to default"))
  {
    editorCameraSettings::writeSpeed(*m_settings, editorCameraSettings::defaultSpeed);
    m_renderer->getCamera()->setSpeed(editorCameraSettings::defaultSpeed);
  }

  ImGui::Spacing();

  bool showGrid = editorViewportSettings::readShowGrid(*m_settings);
  if (gc::accentCheckbox("Show Grid", &showGrid))
  {
    editorViewportSettings::writeShowGrid(*m_settings, showGrid);
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  gc::sectionLabel("Gizmo");

  bool snapEnabled = editorGizmoSettings::readSnapEnabled(*m_settings);
  if (gc::accentCheckbox("Snap", &snapEnabled))
  {
    editorGizmoSettings::writeSnapEnabled(*m_settings, snapEnabled);
  }

  ImGui::Spacing();

  float translateStep = editorGizmoSettings::readTranslateStep(*m_settings);
  if (gc::accentSlider("Translate Step", &translateStep, editorGizmoSettings::minTranslateStep,
                       editorGizmoSettings::maxTranslateStep))
  {
    editorGizmoSettings::writeTranslateStep(*m_settings, translateStep);
  }

  float rotateStep = editorGizmoSettings::readRotateStepDegrees(*m_settings);
  if (gc::accentSlider("Rotate Step (degrees)", &rotateStep, editorGizmoSettings::minRotateStepDegrees,
                       editorGizmoSettings::maxRotateStepDegrees))
  {
    editorGizmoSettings::writeRotateStepDegrees(*m_settings, rotateStep);
  }

  float scaleStep = editorGizmoSettings::readScaleStep(*m_settings);
  if (gc::accentSlider("Scale Step", &scaleStep, editorGizmoSettings::minScaleStep,
                       editorGizmoSettings::maxScaleStep))
  {
    editorGizmoSettings::writeScaleStep(*m_settings, scaleStep);
  }

  ImGui::Spacing();

  if (ImGui::Button("Reset Gizmo to defaults"))
  {
    editorGizmoSettings::writeSnapEnabled(*m_settings, editorGizmoSettings::defaultSnapEnabled);
    editorGizmoSettings::writeTranslateStep(*m_settings, editorGizmoSettings::defaultTranslateStep);
    editorGizmoSettings::writeRotateStepDegrees(*m_settings, editorGizmoSettings::defaultRotateStepDegrees);
    editorGizmoSettings::writeScaleStep(*m_settings, editorGizmoSettings::defaultScaleStep);
  }
}

void SettingsPanel::displayKeybinds()
{
  gc::sectionLabel("Keybinds");

  ImGui::TextColored(theme::t2, "Click Rebind, then press the new key combination.");

  ImGui::Spacing();

  if (ImGui::Button("Reset all to defaults"))
  {
    m_rebindFlow.resetAll();
  }

  ImGui::Spacing();

  constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH;
  if (ImGui::BeginTable("KeybindTable", 3, flags))
  {
    ImGui::TableSetupColumn("Action");
    ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthFixed, bindingColumnWidth());
    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, actionsColumnWidth());
    ImGui::TableHeadersRow();

    const auto scrollTarget = m_rebindFlow.takeScrollTarget();

    for (const auto& info : editorActions())
    {
      ImGui::PushID(info.id);
      ImGui::TableNextRow();

      if (scrollTarget == info.action)
      {
        ImGui::SetScrollHereY();
      }

      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(info.label);

      ImGui::TableSetColumnIndex(1);
      ImGui::AlignTextToFramePadding();
      const bool capturingThis = m_rebindFlow.capturingAction() == info.action;
      if (capturingThis)
      {
        ImGui::TextColored(theme::accent, "%s", capturePrompt);
      }
      else if (const auto chord = m_keybindTable->binding(info.action))
      {
        ImGui::TextUnformatted(formatChord(*chord).c_str());
      }
      else
      {
        ImGui::TextColored(theme::t3, "Unbound");
      }

      ImGui::TableSetColumnIndex(2);
      if (ImGui::Button(capturingThis ? cancelLabel : rebindLabel))
      {
        if (capturingThis)
        {
          m_rebindFlow.cancelCapture();
        }
        else
        {
          m_rebindFlow.beginCapture(info.action);
          armCaptureFor(info.action);
        }
      }

      ImGui::SameLine();
      ImGui::BeginDisabled(!m_keybindTable->binding(info.action));
      if (ImGui::Button(unbindLabel))
      {
        m_rebindFlow.unbind(info.action);
      }
      ImGui::EndDisabled();

      ImGui::SameLine();
      if (ImGui::Button(resetLabel))
      {
        m_rebindFlow.reset(info.action);
      }

      ImGui::PopID();
    }

    ImGui::EndTable();
  }

  displayKeybindConflictModal();
}

void SettingsPanel::armCaptureFor(const EditorAction action)
{
  m_keybindDispatcher->beginCapture([this, action](const KeyChord& chord) {
    m_rebindFlow.captureChord(action, chord);
  });
}

void SettingsPanel::displayKeybindConflictModal()
{
  const auto& conflict = m_rebindFlow.conflict();
  if (!conflict.has_value())
  {
    return;
  }

  constexpr const char* popupName = "Keybind Conflict";

  // Captured before any button below can clear the conflict, so the second button's visibility check never
  // reads through a cleared optional.
  const bool offerChooseAnother = conflict->offerChooseAnother;

  ImGui::OpenPopup(popupName);

  if (ImGui::BeginPopupModal(popupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::Text("%s is already bound to", formatChord(conflict->requested).c_str());
    ImGui::SameLine();
    ImGui::TextColored(theme::accent, "%s", actionInfo(conflict->heldBy).label);
    ImGui::TextColored(theme::t3, "Choose a different key, or go there to change it first.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // No reassign option: displacing the holder would leave a previously working action silently
    // unbound, and the person who caused it is the least likely to notice.
    if (ImGui::Button("Go to binding", ImVec2(150, 0)))
    {
      m_rebindFlow.goToBinding();
      ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();

    if (offerChooseAnother)
    {
      if (ImGui::Button("Choose another key", ImVec2(150, 0)))
      {
        ImGui::CloseCurrentPopup();
        if (const auto action = m_rebindFlow.chooseAnotherKey())
        {
          armCaptureFor(*action);
        }
      }
    }
    else if (ImGui::Button("Close", ImVec2(150, 0)))
    {
      m_rebindFlow.closeConflict();
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }
}
