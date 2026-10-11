#ifndef GRIDVISIBILITY_H
#define GRIDVISIBILITY_H

// The editor grid shows only while the user wants it and the viewport is the editor's own free-fly
// camera; looking through a scene camera mimics a client, which never draws the grid.
[[nodiscard]] inline bool shouldShowGrid(const bool userEnabled, const bool viewingSceneCamera)
{
  return userEnabled && !viewingSceneCamera;
}

#endif
