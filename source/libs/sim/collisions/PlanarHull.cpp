#include "PlanarHull.h"
#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>

namespace {
  // A turn below this share of the product of the two edge lengths is straight.
  constexpr float collinearShare = 1e-5f;

  struct Point {
    float x;
    float y;
    size_t index;
  };

  bool keepsTurn(const Point& o, const Point& a, const Point& b)
  {
    const float turn = (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    const float lengths = std::hypot(a.x - o.x, a.y - o.y) * std::hypot(b.x - o.x, b.y - o.y);

    return turn > collinearShare * lengths;
  }

  void extend(std::vector<Point>& hull, const Point& point, const size_t floor)
  {
    while (hull.size() >= floor && !keepsTurn(hull[hull.size() - 2], hull.back(), point))
    {
      hull.pop_back();
    }

    hull.push_back(point);
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

    std::vector<Point> sorted;
    for (size_t i = 0; i < points.size(); ++i)
    {
      sorted.push_back({ glm::dot(points[i], u), glm::dot(points[i], v), i });
    }
    std::ranges::sort(sorted, [](const Point& a, const Point& b) { return a.x != b.x ? a.x < b.x : a.y < b.y; });

    std::vector<Point> hull;
    for (const auto& point : sorted)
    {
      extend(hull, point, 2);
    }

    const size_t lower = hull.size() + 1;
    for (auto point = sorted.rbegin() + 1; point != sorted.rend(); ++point)
    {
      extend(hull, *point, lower);
    }
    hull.pop_back();

    std::vector<size_t> outline;
    for (const auto& point : hull)
    {
      outline.push_back(point.index);
    }

    return outline;
  }

  std::vector<glm::vec3> convexOutline(const std::span<const glm::vec3> points, const glm::vec3& normal)
  {
    std::vector<glm::vec3> outline;
    for (const auto index : convexOutlineIndices(points, normal))
    {
      outline.push_back(points[index]);
    }

    return outline;
  }
}
