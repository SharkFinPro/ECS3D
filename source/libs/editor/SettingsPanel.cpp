#include "SettingsPanel.h"
#include "EditorTheme.h"
#include "GuiComponents.h"
#include "KeybindDispatcher.h"
#include <Keybinds.h>
#include <SettingsStore.h>
#include <imgui.h>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {
  constexpr const char* windowName = "Settings";
  constexpr const char* openKey = "panels.settings.open";
  constexpr float navWidth = 158.0f;

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

SettingsPanel::SettingsPanel(SettingsStore& settings, std::shared_ptr<KeybindTable> keybindTable,
                             std::shared_ptr<KeybindDispatcher> keybindDispatcher)
  : m_settings(&settings), m_keybindTable(std::move(keybindTable)),
    m_keybindDispatcher(std::move(keybindDispatcher)), m_open(settings.get<bool>(openKey, false))
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
  ImGui::BeginChild("SettingsNav", ImVec2(navWidth, 0.0f), true);

  if (gc::menuRow("Appearance", gc::SecIcon::image, 34.0f, m_section == Section::appearance))
  {
    m_section = Section::appearance;
  }

  if (gc::menuRow("Keybinds", gc::SecIcon::block, 34.0f, m_section == Section::keybinds))
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

void SettingsPanel::displayKeybinds()
{
  gc::sectionLabel("Keybinds");

  ImGui::TextColored(theme::t2, "Click Rebind, then press the new key combination.");

  ImGui::Spacing();

  if (ImGui::Button("Reset all to defaults"))
  {
    m_keybindTable->resetAll(*m_settings);
    m_capturingAction.reset();
  }

  ImGui::Spacing();

  constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH;
  if (ImGui::BeginTable("KeybindTable", 3, flags))
  {
    ImGui::TableSetupColumn("Action");
    ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthFixed, 180.0f);
    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 210.0f);
    ImGui::TableHeadersRow();

    for (const auto& info : editorActions())
    {
      ImGui::PushID(info.id);
      ImGui::TableNextRow();

      if (m_scrollToAction == info.action)
      {
        ImGui::SetScrollHereY();
        m_scrollToAction.reset();
      }

      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(info.label);

      ImGui::TableSetColumnIndex(1);
      ImGui::AlignTextToFramePadding();
      const bool capturingThis = m_capturingAction == info.action;
      if (capturingThis)
      {
        ImGui::TextColored(theme::accent, "Press a key... Esc to cancel");
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
      if (ImGui::Button(capturingThis ? "Cancel" : "Rebind"))
      {
        if (capturingThis)
        {
          m_capturingAction.reset();
        }
        else
        {
          beginCaptureFor(info.action);
        }
      }

      ImGui::SameLine();
      ImGui::BeginDisabled(!m_keybindTable->binding(info.action));
      if (ImGui::Button("Unbind"))
      {
        m_keybindTable->unbind(info.action, *m_settings);
        m_capturingAction.reset();
      }
      ImGui::EndDisabled();

      ImGui::SameLine();
      if (ImGui::Button("Reset"))
      {
        const auto outcome = m_keybindTable->reset(info.action, *m_settings);
        if (outcome.result == KeybindTable::AssignResult::refused)
        {
          m_conflict = KeybindConflict{ info.action, *outcome.heldBy, *actionInfo(info.action).defaultChord, false };
        }
        m_capturingAction.reset();
      }

      ImGui::PopID();
    }

    ImGui::EndTable();
  }

  displayKeybindConflictModal();
}

void SettingsPanel::beginCaptureFor(const EditorAction action)
{
  m_capturingAction = action;

  m_keybindDispatcher->beginCapture([this, action](const KeyChord& chord) {
    m_capturingAction.reset();

    const auto outcome = m_keybindTable->assign(action, chord, *m_settings);
    if (outcome.result == KeybindTable::AssignResult::refused)
    {
      m_conflict = KeybindConflict{ action, *outcome.heldBy, chord };
    }
  });
}

void SettingsPanel::displayKeybindConflictModal()
{
  if (!m_conflict.has_value())
  {
    return;
  }

  constexpr const char* popupName = "Keybind Conflict";

  // Captured before any button below can reset m_conflict, so the second button's visibility check never
  // reads through a cleared optional.
  const bool offerChooseAnother = m_conflict->offerChooseAnother;

  ImGui::OpenPopup(popupName);

  if (ImGui::BeginPopupModal(popupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::Text("%s is already bound to", formatChord(m_conflict->requested).c_str());
    ImGui::SameLine();
    ImGui::TextColored(theme::accent, "%s", actionInfo(m_conflict->heldBy).label);
    ImGui::TextColored(theme::t3, "Choose a different key, or go there to change it first.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // No reassign option: displacing the holder would leave a previously working action silently
    // unbound, and the person who caused it is the least likely to notice.
    if (ImGui::Button("Go to binding", ImVec2(150, 0)))
    {
      m_scrollToAction = m_conflict->heldBy;
      m_conflict.reset();
      ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();

    if (offerChooseAnother)
    {
      if (ImGui::Button("Choose another key", ImVec2(150, 0)))
      {
        const auto action = m_conflict->action;
        m_conflict.reset();
        ImGui::CloseCurrentPopup();
        beginCaptureFor(action);
      }
    }
    else if (ImGui::Button("Close", ImVec2(150, 0)))
    {
      m_conflict.reset();
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }
}
