#ifndef LIGHTGIZMOVISIBILITY_H
#define LIGHTGIZMOVISIBILITY_H

#include <scenes/SceneManager.h>

// Editor light sprites are drawn only while the scene is stopped (running and paused are both play mode)
// and only on the raster path: with ray tracing on, the renderer traces every submitted object as lit
// scene geometry and runs no raster pass to draw an icon in.
[[nodiscard]] inline bool shouldDrawLightGizmos(const SceneStatus status, const bool rayTracingEnabled)
{
  return status == SceneStatus::stopped && !rayTracingEnabled;
}

#endif
