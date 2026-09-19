#ifndef FINITECHECK_H
#define FINITECHECK_H

#include <glm/vec3.hpp>
#include <cmath>

// The component setters drop non-finite input rather than store it: a non-finite float is serialized as
// the json literal null, and loading that back throws away the whole project file.
//
// Not named `finite`: glibc declares a global `finite()`, and a namespace of that name would collide.
namespace finiteCheck {
  [[nodiscard]] inline bool isFinite(const float value)
  {
    return std::isfinite(value);
  }

  [[nodiscard]] inline bool isFinite(const glm::vec3& value)
  {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
  }
}

#endif //FINITECHECK_H
