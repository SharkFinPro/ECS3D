#ifndef SUPPORT_H
#define SUPPORT_H

#include <glm/vec3.hpp>
#include <memory>

class Collider;

// Minkowski-difference support point. This was Collider::getSupport; it lives in ECS3DSim now
// because it is part of the GJK/EPA algorithm, and it only reads the colliders' (data) geometry.
glm::vec3 getSupport(Collider* collider, const std::shared_ptr<Collider>& other, const glm::vec3& direction);



#endif //SUPPORT_H
