#ifndef RAYTRACINGAPPLY_H
#define RAYTRACINGAPPLY_H

#include <VulkanEngine/components/renderingManager/RenderingManager.h>

inline void applyRayTracing(vke::RenderingManager& rendering, const bool enabled)
{
  if (enabled)
  {
    rendering.enableRayTracing();
  }
  else
  {
    rendering.disableRayTracing();
  }
}

#endif  // RAYTRACINGAPPLY_H
