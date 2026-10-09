#ifndef BILLBOARD_H
#define BILLBOARD_H

#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <optional>

// Fraction of the viewport height an editor light sprite spans.
inline constexpr float lightGizmoScreenFraction = 0.05f;

// The rotation that turns a quad's local +X/+Y/+Z onto the camera's right/up/back axes, so every sprite
// shares the camera's orientation: upright, parallel to the screen and facing the eye.
[[nodiscard]] glm::quat billboardOrientation(const glm::mat4& view);

// The world-space edge length at which a quad at position spans screenFraction of the viewport height.
// Depends on view-space depth, not distance, since the projection does. Empty when the position is at or
// inside the near plane (or behind the camera), or any input is non-finite.
[[nodiscard]] std::optional<float> billboardWorldSize(const glm::mat4& view, glm::vec3 position,
                                                      float fovDegrees, float nearPlane, float screenFraction);

#endif //BILLBOARD_H
