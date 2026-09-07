#include "SettingsPanel.h"
#include "EditorTheme.h"
#include "GuiComponents.h"
#include <SettingsStore.h>
#include <imgui.h>
#include <cstddef>
#include <string>
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
  // unreadable. NaN fails the comparison too, which is the point of testing the range rather than
  // clamping to it.
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

SettingsPanel::SettingsPanel(SettingsStore& settings)
  : m_settings(&settings), m_open(settings.get<bool>(openKey, false))
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

  // Begin reports a collapsed or clipped window by returning false without having drawn anything, and
  // the contents have to be skipped in that case - but the close box still has to be honored, since it
  // is what a collapsed window's title bar is mostly made of.
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

  const char* group = nullptr;

  for (std::size_t i = 0; i < theme::tokens().size(); ++i)
  {
    const auto& token = theme::tokens()[i];

    if (group == nullptr || std::string(group) != token.group)
    {
      group = token.group;

      ImGui::Spacing();
      gc::sectionLabel(group);
    }

    // Keyed by index rather than by label: two groups can carry the same label, and ImGui would treat
    // both rows as one widget.
    ImGui::PushID(static_cast<int>(i));

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

void SettingsPanel::displayKeybinds()
{
  gc::sectionLabel("Keybinds");

  gc::emptyState(gc::SecIcon::block, "Keybinds are not remappable yet",
                 "The editor's bindings are fixed. This is where they will be edited.");
}
