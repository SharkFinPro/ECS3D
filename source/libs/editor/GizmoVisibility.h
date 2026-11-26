#ifndef GIZMOVISIBILITY_H
#define GIZMOVISIBILITY_H

// The transform gizmo belongs to the editor's own free-fly camera; looking through a scene camera mimics a
// client, so the gizmo draws nothing and leaves the mouse to the game. Terrain mode owns the left button, so it hides
// the gizmo too.
[[nodiscard]] inline bool shouldDrawTransformGizmo(const bool viewingSceneCamera, const bool terrainModeActive = false)
{
  return !viewingSceneCamera && !terrainModeActive;
}

#endif
