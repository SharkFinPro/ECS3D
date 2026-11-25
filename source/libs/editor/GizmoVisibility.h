#ifndef GIZMOVISIBILITY_H
#define GIZMOVISIBILITY_H

// The transform gizmo belongs to the editor's own free-fly camera; looking through a scene camera mimics a
// client, so the gizmo draws nothing and leaves the mouse to the game.
[[nodiscard]] inline bool shouldDrawTransformGizmo(const bool viewingSceneCamera)
{
  return !viewingSceneCamera;
}

#endif
