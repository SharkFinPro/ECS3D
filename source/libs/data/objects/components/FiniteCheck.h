#ifndef FINITECHECK_H
#define FINITECHECK_H

#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <limits>

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

  // A non-finite float saves as json null, so a saved value may not be a number at all; it reads back as
  // NaN rather than throwing, so the setter's finite check still catches it.
  [[nodiscard]] inline float readFloatOrNaN(const nlohmann::json& value)
  {
    if (!value.is_number())
    {
      return std::numeric_limits<float>::quiet_NaN();
    }

    return value.get<float>();
  }

  [[nodiscard]] inline glm::vec3 readVec3OrNaN(const nlohmann::json& value)
  {
    return glm::vec3(readFloatOrNaN(value.at(0)), readFloatOrNaN(value.at(1)), readFloatOrNaN(value.at(2)));
  }
}

#endif //FINITECHECK_H
