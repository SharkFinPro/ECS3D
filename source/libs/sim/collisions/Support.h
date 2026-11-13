#ifndef SUPPORT_H
#define SUPPORT_H

#include <objects/components/collisions/BoxCollider.h>
#include <objects/components/collisions/CapsuleCollider.h>
#include <objects/components/collisions/ConvexMeshCollider.h>
#include <objects/components/collisions/SphereCollider.h>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <span>

class Collider;

// Minkowski-difference support point. Lives in ECS3DSim because it is part of the GJK/EPA algorithm,
// and it only reads the colliders' (data) geometry.
//
// By reference, not by shared_ptr: this owns neither collider and does not outlive the call, so it has
// no business in their lifetime. The references also cannot be null, which the callers already relied on.
// Not const only because findFurthestPoint is a non-const virtual.
glm::vec3 getSupport(Collider& collider, Collider& other, const glm::vec3& direction);

// A collider's support geometry, refreshed once so GJK and EPA query it without revalidating the collider's
// caches on every call. It answers exactly as findFurthestPoint does, and stays valid while no transform or
// collider geometry changes - for the length of one narrow-phase call.
class SupportShape {
public:
  // With a sweep, the shape answers for everything it passes through when carried along that displacement.
  explicit SupportShape(Collider& collider, const glm::vec3& sweep = glm::vec3(0));

  [[nodiscard]] glm::vec3 furthestPoint(const glm::vec3& direction) const
  {
    const auto point = furthestPointAtRest(direction);

    return glm::dot(direction, m_sweep) > 0.0f ? point + m_sweep : point;
  }

private:
  [[nodiscard]] glm::vec3 furthestPointAtRest(const glm::vec3& direction) const
  {
    switch (m_type)
    {
      case ColliderType::boxCollider:
        return m_box->furthestPoint(direction);
      case ColliderType::sphereCollider:
        return m_sphere.furthestPoint(direction);
      case ColliderType::capsuleCollider:
        return m_capsule.furthestPoint(direction);
      case ColliderType::convexMeshCollider:
        return ConvexMeshCollider::furthestOf(m_hullVertices, direction);
    }

    return glm::vec3(0);
  }

  glm::vec3 m_sweep;
  ColliderType m_type;
  const BoxSupport* m_box = nullptr;
  SphereSupport m_sphere;
  CapsuleSupport m_capsule;
  std::span<const glm::vec3> m_hullVertices;
};

inline glm::vec3 getSupport(const SupportShape& shape, const SupportShape& other, const glm::vec3& direction)
{
  return shape.furthestPoint(direction) - other.furthestPoint(-direction);
}



#endif //SUPPORT_H
