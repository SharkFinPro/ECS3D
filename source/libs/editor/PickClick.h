#ifndef PICKCLICK_H
#define PICKCLICK_H

#include "Selection.h"
#include <functional>
#include <optional>
#include <uuid.h>

// What the editor knows about the mouse when it decides whether a viewport click asks the renderer for a pick.
struct PickClick {
  bool pressed = false;
  bool wasPressed = false;
  bool ctrl = false;
  bool sceneHovered = false;
  bool gizmoCaptured = false;
};

// A plain click focuses the viewport or grabs a gizmo handle, so only a fresh Ctrl+click over the scene that no
// gizmo handle captured picks.
[[nodiscard]] constexpr bool shouldRequestPick(const PickClick& click)
{
  return click.pressed && !click.wasPressed && click.ctrl && click.sceneHovered && !click.gizmoCaptured;
}

// Applies a finished pick to the selection. Plain select replaces, toggle adds or removes, and empty space clears
// unless toggling. A pick of an object that has since left the scene changes nothing.
inline void applyPickResult(EditorSelection& selection, const std::optional<uuids::uuid>& picked, const bool toggle,
                            const std::function<bool(const uuids::uuid&)>& exists)
{
  if (!picked.has_value())
  {
    if (!toggle)
    {
      selection.clear();
    }

    return;
  }

  if (!exists(*picked))
  {
    return;
  }

  if (toggle)
  {
    selection.toggleObject(*picked);
  }
  else
  {
    selection.selectObject(*picked);
  }
}

#endif //PICKCLICK_H
