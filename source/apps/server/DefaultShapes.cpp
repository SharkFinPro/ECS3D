#include "DefaultShapes.h"
#include <InertiaTensor.h>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
  ConvexHull hullOf(const std::vector<glm::vec3>& points, const char* name)
  {
    auto hull = buildConvexHull(points);

    if (!hull)
    {
      throw std::runtime_error(std::string("DefaultShapes: could not build the ") + name + " hull");
    }

    return std::move(*hull);
  }
}

ConvexHull rampHull()
{
  return hullOf({
    { -1, -1, -1 }, { 1, -1, -1 }, { 1, -1, 1 }, { -1, -1, 1 },
    { 1, 1, -1 }, { 1, 1, 1 }
  }, "ramp");
}

ConvexHull cylinderHull()
{
  constexpr int segments = 32;

  std::vector<glm::vec3> points;
  points.reserve(segments * 2);

  for (const float y : { -1.0f, 1.0f })
  {
    for (int k = 0; k < segments; ++k)
    {
      const double angle = 2.0 * std::numbers::pi * k / segments;
      points.emplace_back(static_cast<float>(std::cos(angle)), y, static_cast<float>(std::sin(angle)));
    }
  }

  return hullOf(points, "cylinder");
}

float hullVolume(const ConvexHull& hull)
{
  return inertia::hull(hull.vertices, hull.indices).volume;
}

nlohmann::json convexMeshCollider(const ConvexHull& hull, const std::string& sourceModel)
{
  nlohmann::json vertices = nlohmann::json::array();

  for (const auto& vertex : hull.vertices)
  {
    vertices.push_back(vertex.x);
    vertices.push_back(vertex.y);
    vertices.push_back(vertex.z);
  }

  return {
    { "type", "Collider" },
    { "subType", "ConvexMesh" },
    { "position", { 0.0f, 0.0f, 0.0f } },
    { "rotation", { 0.0f, 0.0f, 0.0f } },
    { "scale", { 1.0f, 1.0f, 1.0f } },
    { "sourceModel", sourceModel },
    { "vertices", std::move(vertices) },
    { "indices", hull.indices },
    { "renderCollider", false },
    { "isTrigger", false },
    { "layer", 0u },
    { "mask", 0xFFFFFFFFu }
  };
}
