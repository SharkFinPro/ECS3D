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
    const float ax = a.x - o.x;
    const float ay = a.y - o.y;
    const float bx = b.x - o.x;
    const float by = b.y - o.y;
    const float turn = ax * by - ay * bx;

    if (turn <= 0.0f)
    {
      return false;
    }

    // Squared form of turn > collinearShare * |a| * |b|, avoiding hypot/sqrt.
    return turn * turn > collinearShare * collinearShare * (ax * ax + ay * ay) * (bx * bx + by * by);
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

    std::vector<Point> sorted;
    sorted.reserve(points.size());
    for (size_t i = 0; i < points.size(); ++i)
    {
      sorted.push_back({ glm::dot(points[i], u), glm::dot(points[i], v), i });
    }
    std::ranges::sort(sorted, [](const Point& a, const Point& b) { return a.x != b.x ? a.x < b.x : a.y < b.y; });

    std::vector<Point> hull(points.size() + 1);
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
    std::vector<glm::vec3> outline;
    for (const auto index : convexOutlineIndices(points, normal))
    {
      outline.push_back(points[index]);
    }

    return outline;
  }
}
