#ifndef TERRAINSURFACEPLAN_H
#define TERRAINSURFACEPLAN_H

#include "TerrainSurfaceSampling.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <optional>

namespace terrain
{
  // The bounds of the surfaces a terrain holds: the one drawn, and a larger one being built to replace it.
  struct SurfaceSlots
  {
    std::optional<SampleBox> current;
    std::optional<SampleBox> replacement;
    // The replacement shows everything reported to it (pendingFraction() == 0).
    bool replacementReady = false;
  };

  enum class SurfaceStep
  {
    keep,               // nothing to change
    createCurrent,      // no surface yet: create one with `bounds` and draw it at once, so terrain fills in as it arrives
    createReplacement,  // the terrain outgrew the drawn surface: build one with `bounds`, keep drawing the old one
    promote,            // the replacement is ready: release the drawn surface, draw the replacement
    tooLarge            // `bounds` needs more than maxSurfaceChunks: draw what is held, create nothing
  };

  struct SurfacePlan
  {
    SurfaceStep step = SurfaceStep::keep;
    SampleBox bounds;
  };

  // desired is surfaceBounds(volume.occupiedBounds()). An empty desired box keeps whatever is held. A ready
  // replacement is promoted before anything else; while a replacement is building, nothing new is created (so a
  // terrain that keeps growing still shows each finished step).
  [[nodiscard]] inline SurfacePlan planTerrainSurface(const SampleBox& desired, const SurfaceSlots& slots)
  {
    if (slots.replacement && slots.replacementReady)
    {
      return { SurfaceStep::promote, *slots.replacement };
    }

    if (desired.empty() || slots.replacement)
    {
      return {};
    }

    if (slots.current && containsBox(*slots.current, desired))
    {
      return {};
    }

    if (surfaceChunkCount(desired) > maxSurfaceChunks)
    {
      return { SurfaceStep::tooLarge, desired };
    }

    return { slots.current ? SurfaceStep::createReplacement : SurfaceStep::createCurrent, desired };
  }

  // Terrain space is metric (sample i sits at i * voxelSize), so the owner's scale is not part of the matrix.
  [[nodiscard]] inline glm::mat4 terrainModelMatrix(const glm::vec3& position, const glm::quat& orientation)
  {
    return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(orientation);
  }
}

#endif //TERRAINSURFACEPLAN_H
