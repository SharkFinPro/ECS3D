#ifndef SUPPORT_H
#define SUPPORT_H

#include <glm/vec3.hpp>

class Collider;

// Minkowski-difference support point. Lives in ECS3DSim because it is part of the GJK/EPA algorithm,
// and it only reads the colliders' (data) geometry.
//
// By reference, not by shared_ptr: this owns neither collider and does not outlive the call, so it has
// no business in their lifetime. The references also cannot be null, which the callers already relied on.
// Not const only because findFurthestPoint is a non-const virtual.
glm::vec3 getSupport(Collider& collider, Collider& other, const glm::vec3& direction);



#endif //SUPPORT_H
