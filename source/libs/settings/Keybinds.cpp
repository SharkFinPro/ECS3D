#include "Keybinds.h"
#include "SettingsStore.h"
#include <algorithm>
#include <cctype>
#include <charconv>

namespace {
  constexpr int modShift = 0x1;
  constexpr int modCtrl = 0x2;
  constexpr int modAlt = 0x4;
  constexpr int modSuper = 0x8;

  struct KeyName {
    int key;
    const char* name;
  };

  // GLFW's own numeric key codes, spelled out here so this library never includes GLFW. See Keybinds.h.
  constexpr std::array<KeyName, 63> keyNames{ {
    { 65, "A" }, { 66, "B" }, { 67, "C" }, { 68, "D" }, { 69, "E" }, { 70, "F" }, { 71, "G" },
    { 72, "H" }, { 73, "I" }, { 74, "J" }, { 75, "K" }, { 76, "L" }, { 77, "M" }, { 78, "N" },
    { 79, "O" }, { 80, "P" }, { 81, "Q" }, { 82, "R" }, { 83, "S" }, { 84, "T" }, { 85, "U" },
    { 86, "V" }, { 87, "W" }, { 88, "X" }, { 89, "Y" }, { 90, "Z" },
    { 48, "0" }, { 49, "1" }, { 50, "2" }, { 51, "3" }, { 52, "4" },
    { 53, "5" }, { 54, "6" }, { 55, "7" }, { 56, "8" }, { 57, "9" },
    { 290, "F1" }, { 291, "F2" }, { 292, "F3" }, { 293, "F4" }, { 294, "F5" }, { 295, "F6" },
    { 296, "F7" }, { 297, "F8" }, { 298, "F9" }, { 299, "F10" }, { 300, "F11" }, { 301, "F12" },
    { 256, "Escape" }, { 257, "Enter" }, { 258, "Tab" }, { 259, "Backspace" }, { 260, "Insert" },
    { 261, "Delete" }, { 268, "Home" }, { 269, "End" }, { 266, "PageUp" }, { 267, "PageDown" },
    { 265, "Up" }, { 264, "Down" }, { 263, "Left" }, { 262, "Right" }, { 32, "Space" }
  } };

  bool ieq(const std::string_view a, const std::string_view b)
  {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](const char x, const char y) {
             return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
  }

  std::string keyFor(const EditorAction action)
  {
    return std::string("keybinds.") + actionInfo(action).id;
  }
}

std::optional<KeyChord> parseChord(const std::string_view text)
{
  if (text.empty())
  {
    return std::nullopt;
  }

  int mods = 0;
  std::string_view keyToken;

  std::size_t start = 0;
  while (true)
  {
    const auto plus = text.find('+', start);
    const auto token = text.substr(start, plus == std::string_view::npos ? std::string_view::npos : plus - start);

    if (plus == std::string_view::npos)
    {
      keyToken = token;
      break;
    }

    if (ieq(token, "Ctrl"))
    {
      mods |= modCtrl;
    }
    else if (ieq(token, "Shift"))
    {
      mods |= modShift;
    }
    else if (ieq(token, "Alt"))
    {
      mods |= modAlt;
    }
    else if (ieq(token, "Super"))
    {
      mods |= modSuper;
    }
    else
    {
      return std::nullopt;
    }

    start = plus + 1;
  }

  if (keyToken.empty())
  {
    return std::nullopt;
  }

  for (const auto& entry : keyNames)
  {
    if (ieq(keyToken, entry.name))
    {
      return KeyChord{ entry.key, mods };
    }
  }

  // "Key<code>" is the fallback spelling formatChord() uses for a key outside the named set above (e.g.
  // a punctuation or numpad key captured by a rebind), so every chord it produces round-trips back here.
  // std::from_chars rather than std::stoi: a hand-edited settings file can carry an out-of-range or
  // malformed number, and stoi throws on that instead of reporting it as unparseable.
  if (keyToken.size() > 3 && ieq(keyToken.substr(0, 3), "Key"))
  {
    const auto digits = keyToken.substr(3);
    int value = 0;
    const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), value);

    if (ec == std::errc{} && ptr == digits.data() + digits.size() && value >= 0)
    {
      return KeyChord{ value, mods };
    }
  }

  return std::nullopt;
}

std::string formatChord(const KeyChord& chord)
{
  std::string result;

  const auto append = [&](const std::string_view name) {
    if (!result.empty())
    {
      result += '+';
    }
    result += name;
  };

  if (chord.mods & modCtrl)
  {
    append("Ctrl");
  }
  if (chord.mods & modShift)
  {
    append("Shift");
  }
  if (chord.mods & modAlt)
  {
    append("Alt");
  }
  if (chord.mods & modSuper)
  {
    append("Super");
  }

  for (const auto& entry : keyNames)
  {
    if (entry.key == chord.key)
    {
      append(entry.name);
      return result;
    }
  }

  append("Key" + std::to_string(chord.key));
  return result;
}

const std::array<EditorActionInfo, editorActionCount>& editorActions()
{
  static const std::array<EditorActionInfo, editorActionCount> actions = [] {
    std::array<EditorActionInfo, editorActionCount> result{};

    const auto set = [&](const EditorAction action, const char* id, const char* label,
                         const std::optional<KeyChord>& chord) {
      result[static_cast<std::size_t>(action)] = { action, id, label, chord };
    };

    set(EditorAction::toggleGui, "toggleGui", "Toggle GUI", parseChord("F10"));
    set(EditorAction::saveProject, "saveProject", "Save Project", parseChord("Ctrl+S"));
    // Not in the spec's action table, but the editor already has this binding; kept rather than regressed.
    set(EditorAction::saveProjectAs, "saveProjectAs", "Save Project As", parseChord("Ctrl+Shift+S"));
    set(EditorAction::undo, "undo", "Undo", parseChord("Ctrl+Z"));
    set(EditorAction::redo, "redo", "Redo", parseChord("Ctrl+Shift+Z"));
    set(EditorAction::deleteSelection, "deleteSelection", "Delete Selection", parseChord("Delete"));
    set(EditorAction::duplicateSelection, "duplicateSelection", "Duplicate Selection", parseChord("Ctrl+D"));
    set(EditorAction::focusSelection, "focusSelection", "Focus Selection", parseChord("F"));
    set(EditorAction::gizmoTranslate, "gizmoTranslate", "Gizmo: Translate", std::nullopt);
    set(EditorAction::gizmoRotate, "gizmoRotate", "Gizmo: Rotate", std::nullopt);
    set(EditorAction::gizmoScale, "gizmoScale", "Gizmo: Scale", std::nullopt);

    return result;
  }();

  return actions;
}

const EditorActionInfo& actionInfo(const EditorAction action)
{
  return editorActions()[static_cast<std::size_t>(action)];
}

KeybindTable::KeybindTable()
{
  for (const auto& info : editorActions())
  {
    m_bindings[static_cast<std::size_t>(info.action)] = info.defaultChord;
  }
}

void KeybindTable::load(const SettingsStore& settings)
{
  // A marker no hand-written value can equal, so an absent key is distinguishable from one explicitly
  // set to "" (unbound).
  static const std::string absentMarker = "\x01__absent__";

  struct Candidate {
    std::optional<KeyChord> chord;
    bool fromStore = false;
  };

  std::array<Candidate, editorActionCount> candidates;

  for (const auto& info : editorActions())
  {
    const auto raw = settings.get<std::string>(keyFor(info.action), absentMarker);
    const auto index = static_cast<std::size_t>(info.action);

    if (raw == absentMarker)
    {
      candidates[index] = { info.defaultChord, false };
    }
    else if (raw.empty())
    {
      candidates[index] = { std::nullopt, true };
    }
    else if (const auto parsed = parseChord(raw))
    {
      candidates[index] = { parsed, true };
    }
    else
    {
      // Garbage in the file: fall back to the default as if the key were absent.
      candidates[index] = { info.defaultChord, false };
    }
  }

  // A store-sourced value outranks a default; within the same source, declaration order (lower index)
  // wins. The loser of a collision is left unbound rather than displacing the winner.
  const auto higherPriority = [&](const std::size_t i, const std::size_t j) {
    if (candidates[i].fromStore != candidates[j].fromStore)
    {
      return candidates[i].fromStore;
    }
    return i < j;
  };

  for (std::size_t i = 0; i < editorActionCount; ++i)
  {
    const auto& candidate = candidates[i];
    if (!candidate.chord)
    {
      m_bindings[i] = std::nullopt;
      continue;
    }

    bool outranked = false;
    for (std::size_t j = 0; j < editorActionCount; ++j)
    {
      if (j != i && candidates[j].chord == candidate.chord && higherPriority(j, i))
      {
        outranked = true;
        break;
      }
    }

    m_bindings[i] = outranked ? std::nullopt : candidate.chord;
  }
}

std::optional<KeyChord> KeybindTable::binding(const EditorAction action) const
{
  return m_bindings[static_cast<std::size_t>(action)];
}

std::optional<EditorAction> KeybindTable::actionFor(const KeyChord& chord) const
{
  for (const auto& info : editorActions())
  {
    if (m_bindings[static_cast<std::size_t>(info.action)] == chord)
    {
      return info.action;
    }
  }

  return std::nullopt;
}

KeybindTable::AssignOutcome KeybindTable::assign(const EditorAction action, const KeyChord& chord,
                                                 SettingsStore& settings)
{
  if (const auto holder = actionFor(chord); holder && *holder != action)
  {
    return { AssignResult::refused, holder };
  }

  m_bindings[static_cast<std::size_t>(action)] = chord;
  settings.set(keyFor(action), formatChord(chord));

  return { AssignResult::assigned, std::nullopt };
}

void KeybindTable::unbind(const EditorAction action, SettingsStore& settings)
{
  m_bindings[static_cast<std::size_t>(action)] = std::nullopt;
  settings.set(keyFor(action), std::string());
}

KeybindTable::AssignOutcome KeybindTable::reset(const EditorAction action, SettingsStore& settings)
{
  const auto defaultChord = actionInfo(action).defaultChord;

  if (defaultChord)
  {
    if (const auto holder = actionFor(*defaultChord); holder && *holder != action)
    {
      return { AssignResult::refused, holder };
    }
  }

  m_bindings[static_cast<std::size_t>(action)] = defaultChord;
  settings.clear(keyFor(action));

  return { AssignResult::assigned, std::nullopt };
}

void KeybindTable::resetAll(SettingsStore& settings)
{
  for (const auto& info : editorActions())
  {
    m_bindings[static_cast<std::size_t>(info.action)] = info.defaultChord;
    settings.clear(keyFor(info.action));
  }
}
