#include "Billboard.h"
#include <cmath>
#include <glm/mat3x3.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec4.hpp>

glm::quat billboardOrientation(const glm::mat4& view)
{
  return glm::quat_cast(glm::transpose(glm::mat3(view)));
}

std::optional<float> billboardWorldSize(const glm::mat4& view, const glm::vec3 position,
                                        const float fovDegrees, const float nearPlane, const float screenFraction)
{
  const float depth = -(view * glm::vec4(position, 1.0f)).z;
  const float size = 2.0f * depth * std::tan(glm::radians(fovDegrees) / 2.0f) * screenFraction;

  if (!std::isfinite(depth) || !std::isfinite(nearPlane) || !std::isfinite(size) || depth <= nearPlane)
  {
    return std::nullopt;
  }

  return size;
}
