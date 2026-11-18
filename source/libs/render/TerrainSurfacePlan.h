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
    // pendingFraction() of each held surface. The defaults make a replacement count as less complete than the current
    // surface, so nothing is promoted unless the caller reports otherwise.
    float currentPending = 0.0f;
    float replacementPending = 1.0f;
  };

  enum class SurfaceStep
  {
    keep,               // nothing to change
    createCurrent,      // no surface yet: create one with `bounds` and draw it at once, so terrain fills in
    createReplacement,  // the terrain outgrew the drawn surface: build one with `bounds`, keep drawing the old one
    promote,            // the replacement is no less complete than the drawn surface: swap them
    tooLarge            // `bounds` needs more than maxSurfaceChunks: draw what is held, create nothing
  };

  struct SurfacePlan
  {
    SurfaceStep step = SurfaceStep::keep;
    SampleBox bounds;
  };

  // desired is surfaceBounds(volume.occupiedBounds()). An empty desired box keeps whatever is held. A replacement
  // that is no less complete than the drawn surface is promoted before anything else, so a terrain whose chunks keep
  // changing (and keep the replacement pending) still gets its larger surface drawn; while a replacement is
  // building, nothing new is created.
  [[nodiscard]] inline SurfacePlan planTerrainSurface(const SampleBox& desired, const SurfaceSlots& slots)
  {
    if (slots.replacement && slots.replacementPending <= slots.currentPending)
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

  // What the inspector shows about a terrain's surface.
  struct TerrainSurfaceStatus
  {
    bool hasSurface = false;
    // pendingFraction of the drawn surface, or of the replacement while one builds
    float pending = 0.0f;
    bool growing = false;
    bool tooLarge = false;
  };

  // Terrain space is metric (sample i sits at i * voxelSize), so the owner's scale is not part of the matrix.
  [[nodiscard]] inline glm::mat4 terrainModelMatrix(const glm::vec3& position, const glm::quat& orientation)
  {
    return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(orientation);
  }
}

#endif //TERRAINSURFACEPLAN_H
