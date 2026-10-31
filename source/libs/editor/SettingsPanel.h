#ifndef SETTINGSPANEL_H
#define SETTINGSPANEL_H

#include "KeybindRebindFlow.h"
#include <Keybinds.h>
#include <memory>

class SettingsStore;
class KeybindTable;
class KeybindDispatcher;

namespace vke {
  class VulkanEngine;
}

// The editor's user-preferences panel: a left nav of sections beside the selected section's content.
// A dockable window like every other editor surface, never a modal, and there is no Apply step - an
// edit takes effect on the next frame and the store's debounced write persists it.
//
// User preferences only. Project configuration is replicated state and goes through the editor's normal
// edit path instead.
class SettingsPanel {
public:
  // The store outlives the panel: both are owned by the editor app. renderer/keybindTable/
  // keybindDispatcher are shared with EditorApp the way every other subsystem is; renderer is where a
  // Viewport edit applies live, through its vke::Camera.
  SettingsPanel(SettingsStore& settings, std::shared_ptr<vke::VulkanEngine> renderer,
               std::shared_ptr<KeybindTable> keybindTable,
               std::shared_ptr<KeybindDispatcher> keybindDispatcher);

  // Copies the stored Appearance overrides onto the theme tokens. Call before the first applyStyle(),
  // so the editor comes up in the theme it was left in rather than repainting on the first frame.
  static void applyStoredTheme(const SettingsStore& settings);

  void displayGui();

  // Shows the panel and asks ImGui to focus it, so the menu entry works whether or not it was already
  // open behind another window in the same dock node.
  void open();

  [[nodiscard]] bool isOpen() const;

  // Persists the new state alongside it, so the editor comes back up as it was left.
  void setOpen(bool open);

private:
  enum class Section {
    appearance,
    viewport,
    keybinds
  };

  SettingsStore* m_settings;

  std::shared_ptr<vke::VulkanEngine> m_renderer;

  std::shared_ptr<KeybindTable> m_keybindTable;
  std::shared_ptr<KeybindDispatcher> m_keybindDispatcher;
  KeybindRebindFlow m_rebindFlow;

  Section m_section = Section::appearance;

  bool m_open;
  bool m_focusRequested = false;

  void displayNav();

  void displayAppearance();

  void displayViewport();

  void displayKeybinds();

  void displayKeybindConflictModal();

  // Arms the dispatcher key capture; the chord it produces goes to m_rebindFlow.
  void armCaptureFor(EditorAction action);
};

#endif  // SETTINGSPANEL_H
