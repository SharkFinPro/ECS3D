#include "DefaultShapes.h"
#include <InertiaTensor.h>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdint>
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

namespace {
  const double phi = (1.0 + std::sqrt(5.0)) / 2.0;

  glm::vec3 scaled(const double x, const double y, const double z, const double divisor = 1.0)
  {
    return { static_cast<float>(x / divisor), static_cast<float>(y / divisor), static_cast<float>(z / divisor) };
  }

  std::vector<glm::vec3> prismPoints(const double radius, const double halfHeight, const int sides)
  {
    std::vector<glm::vec3> points;
    points.reserve(sides * 2);

    for (const double y : { -halfHeight, halfHeight })
    {
      for (int k = 0; k < sides; ++k)
      {
        const double angle = 2.0 * std::numbers::pi * k / sides;
        points.push_back(scaled(radius * std::cos(angle), y, radius * std::sin(angle)));
      }
    }

    return points;
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

ConvexHull d4Hull()
{
  return hullOf({ { 1, 1, 1 }, { 1, -1, -1 }, { -1, 1, -1 }, { -1, -1, 1 } }, "D4");
}

ConvexHull d6Hull()
{
  std::vector<glm::vec3> points;
  for (const float x : { -1.0f, 1.0f })
  {
    for (const float y : { -1.0f, 1.0f })
    {
      for (const float z : { -1.0f, 1.0f })
      {
        points.emplace_back(x, y, z);
      }
    }
  }

  return hullOf(points, "D6");
}

ConvexHull d8Hull()
{
  return hullOf({ { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } }, "D8");
}

ConvexHull d12Hull()
{
  const double inverse = 1.0 / phi;

  std::vector<glm::vec3> points;
  for (const double x : { -1.0, 1.0 })
  {
    for (const double y : { -1.0, 1.0 })
    {
      for (const double z : { -1.0, 1.0 })
      {
        points.push_back(scaled(x, y, z, phi));
      }
    }
  }

  for (const double a : { -1.0, 1.0 })
  {
    for (const double b : { -1.0, 1.0 })
    {
      points.push_back(scaled(0.0, a * inverse, b * phi, phi));
      points.push_back(scaled(a * inverse, b * phi, 0.0, phi));
      points.push_back(scaled(b * phi, 0.0, a * inverse, phi));
    }
  }

  return hullOf(points, "D12");
}

ConvexHull d20Hull()
{
  std::vector<glm::vec3> points;
  for (const double a : { -1.0, 1.0 })
  {
    for (const double b : { -1.0, 1.0 })
    {
      points.push_back(scaled(0.0, a, b * phi, phi));
      points.push_back(scaled(a, b * phi, 0.0, phi));
      points.push_back(scaled(b * phi, 0.0, a, phi));
    }
  }

  return hullOf(points, "D20");
}

ConvexHull hexPrismHull()
{
  return hullOf(prismPoints(0.6, 1.0, 6), "hex prism");
}

ConvexHull drumHull()
{
  return hullOf(prismPoints(1.0, 0.45, 16), "drum");
}

ConvexHull gemHull()
{
  std::vector<glm::vec3> points{ { 0.0f, -1.0f, 0.0f } };

  for (const auto& [y, radius] : { std::pair{ 0.1, 1.0 }, std::pair{ 0.55, 0.55 } })
  {
    for (int k = 0; k < 8; ++k)
    {
      const double angle = 2.0 * std::numbers::pi * k / 8;
      points.push_back(scaled(radius * std::cos(angle), y, radius * std::sin(angle)));
    }
  }

  return hullOf(points, "gem");
}

std::vector<glm::vec3> boulderPoints()
{
  constexpr int count = 24;
  constexpr double squash = 0.7;
  constexpr double minRadius = 0.75;
  constexpr double jitter = 0.2;

  uint32_t state = 12345u;
  const auto uniform = [&state]()
  {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;

    return static_cast<double>(state) / 4294967296.0;
  };

  const double golden = std::numbers::pi * (3.0 - std::sqrt(5.0));

  std::vector<glm::vec3> points;
  points.reserve(count);

  for (int i = 0; i < count; ++i)
  {
    const double height = 1.0 - 2.0 * (i + 0.5) / count;
    const double ring = std::sqrt(1.0 - height * height);
    const double angle = i * golden;
    const double jx = (uniform() - 0.5) * 2.0 * jitter;
    const double jy = (uniform() - 0.5) * 2.0 * jitter;
    const double jz = (uniform() - 0.5) * 2.0 * jitter;
    const double x = ring * std::cos(angle) + jx;
    const double y = height + jy;
    const double z = ring * std::sin(angle) + jz;
    const double length = std::sqrt(x * x + y * y + z * z);
    const double radius = minRadius + (1.0 - minRadius) * uniform();

    points.push_back(scaled(x / length * radius, y / length * radius * squash, z / length * radius));
  }

  return points;
}

ConvexHull boulderHull()
{
  return hullOf(boulderPoints(), "boulder");
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
