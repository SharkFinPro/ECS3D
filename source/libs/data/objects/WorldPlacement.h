#ifndef WORLDPLACEMENT_H
#define WORLDPLACEMENT_H

#include <glm/vec3.hpp>
#include <memory>
#include <optional>

class Object;

// A world-space snapshot of one object's transform, taken before it is detached, so that after it is
// reattached under a different parent its local values can be rewritten to leave it where it was.
struct WorldPlacement {
  glm::vec3 position;
  glm::vec3 rotation;
  glm::vec3 scale;
};

[[nodiscard]] std::optional<WorldPlacement> captureWorldPlacement(const std::shared_ptr<Object>& object);

// Transform's local values are relative to the parent (see Transform.h/.cpp), so reattaching an
// object under a different parent without rewriting them changes its world placement by the
// difference between the old and new parent's world transform. Called after the reparent with the
// object's own world placement from just before it was detached, this rewrites the local values so
// the world placement is unchanged.
void restoreWorldPlacement(const std::shared_ptr<Object>& object,
                           const std::shared_ptr<Object>& newParent,
                           const WorldPlacement& placement);

#endif //WORLDPLACEMENT_H
