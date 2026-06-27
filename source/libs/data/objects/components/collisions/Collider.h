#ifndef COLLIDER_H
#define COLLIDER_H

#include "../Component.h"
#include <glm/vec3.hpp>
#include <cstdint>
#include <memory>

class Transform;

struct BoundingBox {
  uint8_t lastUpdateID = 0;
  float minX{};
  float maxX{};
  float minY{};
  float maxY{};
  float minZ{};
  float maxZ{};
};

enum class ColliderType {
  boxCollider,
  sphereCollider
};

// Data-only: shape geometry (findFurthestPoint, bounding box). GJK/EPA narrow phase lives in CollisionSystem.
class Collider : public Component {
public:
  explicit Collider(ColliderType type, ComponentType subType);

  const BoundingBox& getBoundingBox();

  [[nodiscard]] ColliderType getColliderType() const;

  virtual glm::vec3 findFurthestPoint(const glm::vec3& direction) = 0;

  [[nodiscard]] virtual glm::vec3 getPosition() { return glm::vec3(0); }
  [[nodiscard]] virtual glm::vec3 getScale() { return glm::vec3(1); }
  [[nodiscard]] virtual glm::vec3 getRotation() { return glm::vec3(0); }

protected:
  std::weak_ptr<Transform> m_transform_ptr;

  ColliderType m_colliderType;

  BoundingBox m_boundingBox;
};



#endif //COLLIDER_H
