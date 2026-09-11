#ifndef SETTINGSPANEL_H
#define SETTINGSPANEL_H

#include <Keybinds.h>
#include <memory>
#include <optional>

class SettingsStore;
class KeybindTable;
class KeybindDispatcher;

// The editor's user-preferences panel: a left nav of sections beside the selected section's content.
// A dockable window like every other editor surface, never a modal, and there is no Apply step - an
// edit takes effect on the next frame and the store's debounced write persists it.
//
// User preferences only. Project configuration is replicated state and goes through the editor's normal
// edit path instead.
class SettingsPanel {
public:
  // The store outlives the panel: both are owned by the editor app. keybindTable/keybindDispatcher are
  // shared with EditorApp the way every other subsystem is.
  SettingsPanel(SettingsStore& settings, std::shared_ptr<KeybindTable> keybindTable,
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
    keybinds
  };

  // A refused rebind or reset: the chord `requested` for `action` is already held by `heldBy`. Shown as
  // a modal with no reassign option, per spec - displacing the holder would leave it silently unbound.
  // offerChooseAnother is false for a refused reset: there is no in-progress capture to redirect into a
  // new key, so the modal offers only "Go to binding" and a close button.
  struct KeybindConflict {
    EditorAction action;
    EditorAction heldBy;
    KeyChord requested;
    bool offerChooseAnother = true;
  };

  SettingsStore* m_settings;

  std::shared_ptr<KeybindTable> m_keybindTable;
  std::shared_ptr<KeybindDispatcher> m_keybindDispatcher;

  Section m_section = Section::appearance;

  bool m_open;
  bool m_focusRequested = false;

  // Rebind-in-progress state for the Keybinds section.
  std::optional<EditorAction> m_capturingAction;
  std::optional<EditorAction> m_scrollToAction;
  std::optional<KeybindConflict> m_conflict;

  void displayNav();

  void displayAppearance();

  void displayKeybinds();

  void displayKeybindConflictModal();

  void beginCaptureFor(EditorAction action);
};

#endif  // SETTINGSPANEL_H
