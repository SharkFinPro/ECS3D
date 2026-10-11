#include "ConvexHullFromModel.h"
#include <objects/components/collisions/ConvexHull.h>
#include <objects/components/collisions/ConvexMeshCollider.h>
#include <GpuAssetCache.h>
#include <Log.h>
#include <VulkanEngine/components/assets/objects/Model.h>
#include <glm/vec3.hpp>
#include <exception>
#include <optional>
#include <string>
#include <vector>

namespace {
  // nullopt (already logged) when the model cannot be resolved or loaded.
  std::optional<std::vector<glm::vec3>> modelPoints(GpuAssetCache& assetCache, const uuids::uuid& modelUUID)
  {
    try
    {
      const auto model = assetCache.getModel(modelUUID);
      if (!model)
      {
        Log::warn(LogCategory::editor, "Convex hull: the source model is not a known asset");
        return std::nullopt;
      }

      std::vector<glm::vec3> points;
      points.reserve(model->getVertices().size());

      for (const auto& vertex : model->getVertices())
      {
        points.push_back(vertex.pos);
      }

      return points;
    }
    catch (const std::exception& e)
    {
      Log::warn(LogCategory::editor, std::string("Convex hull: the source model could not be loaded: ") + e.what());
      return std::nullopt;
    }
  }
}

bool rebuildHullFromModel(ConvexMeshCollider& collider, GpuAssetCache* assetCache, const uuids::uuid& modelUUID)
{
  if (!assetCache || modelUUID.is_nil())
  {
    Log::warn(LogCategory::editor, "Convex hull: there is no source model to build from");
    return false;
  }

  const auto points = modelPoints(*assetCache, modelUUID);
  if (!points)
  {
    return false;
  }

  auto hull = buildConvexHull(*points);
  if (!hull || !collider.setHull(std::move(*hull)))
  {
    Log::warn(LogCategory::editor, "Convex hull: the model has no volume to build a hull from");
    return false;
  }

  collider.setSourceModel(modelUUID);

  return true;
}
