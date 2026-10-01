#ifndef ROTATIONCONVENTION_H
#define ROTATIONCONVENTION_H

#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

// The one Euler (degrees) <-> orientation mapping every system shares. glm's Euler constructor composes
// Rz * Ry * Rx, the order the renderer, colliders and physics all apply.
[[nodiscard]] inline glm::quat eulerDegreesToQuat(const glm::vec3& degrees)
{
  return glm::quat(glm::radians(degrees));
}

[[nodiscard]] inline glm::vec3 quatToEulerDegrees(const glm::quat& orientation)
{
  return glm::degrees(glm::eulerAngles(orientation));
}

#endif //ROTATIONCONVENTION_H
