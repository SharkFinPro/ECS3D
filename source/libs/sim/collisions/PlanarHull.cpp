#include "PlanarHull.h"
#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {
  // A turn below this share of the product of the two edge lengths is straight.
  constexpr float collinearShare = 1e-5f;

  struct Point {
    float x;
    float y;
    uint32_t index;
  };

  bool keepsTurn(const Point& o, const Point& a, const Point& b)
  {
    // hypot rather than a squared comparison, which rounds differently at the collinear threshold.
    const float turn = (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    const float lengths = std::hypot(a.x - o.x, a.y - o.y) * std::hypot(b.x - o.x, b.y - o.y);

    return turn > collinearShare * lengths;
  }

  void extend(Point* hull, size_t& count, const Point& point, const size_t floor)
  {
    while (count >= floor && !keepsTurn(hull[count - 2], hull[count - 1], point))
    {
      --count;
    }

    hull[count++] = point;
  }
}

namespace collisions {
  std::vector<size_t> convexOutlineIndices(const std::span<const glm::vec3> points, const glm::vec3& normal)
  {
    if (points.size() < 3)
    {
      std::vector<size_t> all(points.size());
      std::ranges::generate(all, [next = size_t{ 0 }]() mutable { return next++; });
      return all;
    }

    const auto helper = std::fabs(normal.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    const auto u = glm::normalize(glm::cross(helper, normal));
    const auto v = glm::cross(normal, u);

    // Per-thread scratch keeps capacity across calls, so the hot path does not allocate.
    thread_local std::vector<Point> sorted;
    thread_local std::vector<Point> hull;

    sorted.clear();
    for (size_t i = 0; i < points.size(); ++i)
    {
      sorted.push_back({ glm::dot(points[i], u), glm::dot(points[i], v), static_cast<uint32_t>(i) });
    }
    std::ranges::sort(sorted, [](const Point& a, const Point& b) { return a.x != b.x ? a.x < b.x : a.y < b.y; });

    if (hull.size() < points.size() + 1)
    {
      hull.resize(points.size() + 1);
    }
    size_t count = 0;
    for (const auto& point : sorted)
    {
      extend(hull.data(), count, point, 2);
    }

    const size_t lower = count + 1;
    for (auto point = sorted.rbegin() + 1; point != sorted.rend(); ++point)
    {
      extend(hull.data(), count, *point, lower);
    }
    --count;

    std::vector<size_t> outline(count);
    for (size_t i = 0; i < count; ++i)
    {
      outline[i] = hull[i].index;
    }

    return outline;
  }

  std::vector<glm::vec3> convexOutline(const std::span<const glm::vec3> points, const glm::vec3& normal)
  {
    const auto indices = convexOutlineIndices(points, normal);

    std::vector<glm::vec3> outline;
    outline.reserve(indices.size());
    for (const auto index : indices)
    {
      outline.push_back(points[index]);
    }

    return outline;
  }
}
