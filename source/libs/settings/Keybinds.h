#ifndef KEYBINDS_H
#define KEYBINDS_H

#include <array>
#include <optional>
#include <string>
#include <string_view>

class SettingsStore;

// A key plus GLFW's modifier bitmask (Shift=0x1, Ctrl=0x2, Alt=0x4, Super=0x8). Both fields are GLFW's
// own numeric values, spelled out as literals in Keybinds.cpp so this library never includes GLFW
// (settings must stay dependency-free); the editor lib static_asserts a representative set of them
// against the real macros so a future GLFW upgrade that renumbers any of them fails the build.
struct KeyChord {
  int key = 0;
  int mods = 0;

  friend bool operator==(const KeyChord&, const KeyChord&) = default;
};

// Parses a stable name like "F10" or "Ctrl+Shift+Z" into a chord (case-insensitive, canonical modifier
// order Ctrl+Shift+Alt+Super). A bare modifier name ("Ctrl") is not a valid chord on its own.
[[nodiscard]] std::optional<KeyChord> parseChord(std::string_view text);

// The inverse of parseChord: formatChord(*parseChord(x)) is always the canonical spelling of x.
[[nodiscard]] std::string formatChord(const KeyChord& chord);

// The full catalogue of named editor actions a key chord can be bound to. Actions whose behavior does
// not exist yet (undo/redo/gizmo) are still bindable, so a later feature binds into the existing table
// rather than adding its own hardcoded handler.
enum class EditorAction {
  toggleGui,
  saveProject,
  saveProjectAs,
  undo,
  redo,
  deleteSelection,
  duplicateSelection,
  focusSelection,
  gizmoTranslate,
  gizmoRotate,
  gizmoScale,
  count  // sentinel: the number of real actions above, not a real action itself
};

struct EditorActionInfo {
  EditorAction action;
  const char* id;     // stable settings-key fragment, e.g. "toggleGui"
  const char* label;  // display label, e.g. "Toggle GUI"
  std::optional<KeyChord> defaultChord;
};

inline constexpr std::size_t editorActionCount = static_cast<std::size_t>(EditorAction::count);

// Declaration order doubles as the tie-break order load() falls back to, and as the array index
// (info.action's underlying value == its index).
[[nodiscard]] const std::array<EditorActionInfo, editorActionCount>& editorActions();

[[nodiscard]] const EditorActionInfo& actionInfo(EditorAction action);

// A bijective action <-> chord table: a bound chord belongs to exactly one action. Constructed with the
// compiled-in defaults; load() then overlays whatever the settings file has.
class KeybindTable {
public:
  KeybindTable();

  enum class AssignResult { assigned, refused };

  struct AssignOutcome {
    AssignResult result;
    // Set only when refused: the action already holding the requested chord.
    std::optional<EditorAction> heldBy;
  };

  // Reads "keybinds.<id>" for every action: a stored chord string binds it, an empty string means
  // explicitly unbound, and an absent key follows the compiled-in default. An unparseable string falls
  // back to the default. If two actions would end up on the same chord, a value this file actually wrote
  // wins over one that came from a compiled-in default, then catalogue declaration order; the loser is
  // left unbound - so a hand-edited file always loads to a valid bijection.
  void load(const SettingsStore& settings);

  [[nodiscard]] std::optional<KeyChord> binding(EditorAction action) const;

  [[nodiscard]] std::optional<EditorAction> actionFor(const KeyChord& chord) const;

  // Refuses (leaving both actions' bindings untouched) when chord is already held by a different action.
  AssignOutcome assign(EditorAction action, const KeyChord& chord, SettingsStore& settings);

  // A valid, persisted state distinct from reset: the action follows no chord until reassigned. This is
  // how a chord is freed for another action to take.
  void unbind(EditorAction action, SettingsStore& settings);

  // Restores the action's compiled-in default chord, refusing (leaving both actions' bindings and the
  // store untouched) when a different action currently holds that chord - same semantics as assign().
  // An action whose default is unbound (the gizmo actions) always succeeds, simply clearing whatever
  // chord it held.
  AssignOutcome reset(EditorAction action, SettingsStore& settings);

  void resetAll(SettingsStore& settings);

private:
  std::array<std::optional<KeyChord>, editorActionCount> m_bindings;
};

#endif  // KEYBINDS_H
