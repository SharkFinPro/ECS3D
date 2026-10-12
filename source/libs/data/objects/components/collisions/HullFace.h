#ifndef HULLFACE_H
#define HULLFACE_H

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <array>
#include <cstdint>
#include <vector>

namespace hull {
  struct HullFace {
    std::array<uint32_t, 3> v{};
    glm::dvec3 normal{ 0.0 };
    double offset = 0.0;
    std::vector<uint32_t> outside;
    bool alive = true;
  };

  [[nodiscard]] inline uint64_t edgeKey(const uint32_t from, const uint32_t to)
  {
    return (static_cast<uint64_t>(from) << 32) | to;
  }

  [[nodiscard]] inline double distanceTo(const HullFace& face, const glm::dvec3& point)
  {
    return glm::dot(face.normal, point) - face.offset;
  }
}

#endif //HULLFACE_H
