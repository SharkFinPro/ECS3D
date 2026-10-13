#include "ConvexHullRebuild.h"
#include <objects/components/collisions/ConvexHull.h>
#include <objects/components/collisions/ConvexMeshCollider.h>
#include <Log.h>
#include <utility>

bool rebuildHullFromPoints(ConvexMeshCollider& collider, const std::span<const glm::vec3> points,
                           const uuids::uuid& sourceModel, const bool reportUnchanged)
{
  auto hull = buildConvexHull(points);
  if (!hull)
  {
    Log::warn(LogCategory::editor, "Convex hull: the model has no volume to build a hull from");
    return false;
  }

  const auto& current = collider.getHull();
  const bool sameHull = hull->vertices == current.vertices && hull->indices == current.indices;
  if (sameHull && collider.getSourceModel() == sourceModel)
  {
    return reportUnchanged;
  }

  if (!collider.setHull(std::move(*hull)))
  {
    Log::warn(LogCategory::editor, "Convex hull: the collider refused the hull built from the model");
    return false;
  }

  collider.setSourceModel(sourceModel);

  return true;
}
