#include "Support.h"
#include <objects/components/collisions/Collider.h>

glm::vec3 getSupport(Collider& collider, Collider& other, const glm::vec3& direction)
{
  return collider.findFurthestPoint(direction) - other.findFurthestPoint(-direction);
}

SupportShape::SupportShape(Collider& collider)
  : m_type(collider.getColliderType())
{
  switch (m_type)
  {
    case ColliderType::boxCollider:
      m_box = &static_cast<BoxCollider&>(collider).getWorldSupport();
      break;
    case ColliderType::sphereCollider:
      m_sphere = static_cast<SphereCollider&>(collider).getWorldSupport();
      break;
    case ColliderType::capsuleCollider:
      m_capsule = static_cast<CapsuleCollider&>(collider).getWorldSupport();
      break;
    case ColliderType::convexMeshCollider:
      m_hullVertices = static_cast<ConvexMeshCollider&>(collider).getWorldVertices();
      break;
  }
}
