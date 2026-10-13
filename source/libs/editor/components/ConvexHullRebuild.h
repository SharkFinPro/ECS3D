#ifndef CONVEXHULLREBUILD_H
#define CONVEXHULLREBUILD_H

#include <glm/vec3.hpp>
#include <span>
#include <uuid.h>

class ConvexMeshCollider;

// Stores the hull of `points`, with `sourceModel` as its source, in the collider. Returns true only when
// the collider changed. Points with no volume are logged as a warning and leave the collider as it was, and
// so does a rebuild that yields the hull and source the collider already has, unless reportUnchanged asks
// for true then (a drop that must reach other selected objects).
[[nodiscard]] bool rebuildHullFromPoints(ConvexMeshCollider& collider, std::span<const glm::vec3> points,
                                         const uuids::uuid& sourceModel, bool reportUnchanged = false);

#endif //CONVEXHULLREBUILD_H
