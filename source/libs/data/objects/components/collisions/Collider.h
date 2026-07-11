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

  // A trigger still produces collision events but no physical response (no MTV correction, no
  // impulses) - a volume scripts can react to without it blocking anything. Shared by every collider
  // shape; each subclass threads it through its own serialize/loadFromJSON/pack/unpack.
  [[nodiscard]] bool isTrigger() const;
  void setIsTrigger(bool isTrigger);

  // Broad-phase filtering. layer is this collider's layer index (0-31); mask is the set of layers it
  // collides with (bit N = layer N). Two colliders interact only if each one's mask includes the other's
  // layer, so a pair on non-matching layers is skipped before any narrow-phase / event work. Threaded
  // through each subclass's serialize/loadFromJSON/pack/unpack like isTrigger.
  [[nodiscard]] uint32_t getLayer() const;
  void setLayer(uint32_t layer);

  [[nodiscard]] uint32_t getMask() const;
  void setMask(uint32_t mask);

  virtual glm::vec3 findFurthestPoint(const glm::vec3& direction) = 0;

  [[nodiscard]] virtual glm::vec3 getPosition() { return glm::vec3(0); }
  [[nodiscard]] virtual glm::vec3 getScale() { return glm::vec3(1); }
  [[nodiscard]] virtual glm::vec3 getRotation() { return glm::vec3(0); }

protected:
  std::weak_ptr<Transform> m_transform_ptr;

  ColliderType m_colliderType;

  BoundingBox m_boundingBox;

  bool m_isTrigger = false;

  uint32_t m_layer = 0;
  uint32_t m_mask = 0xFFFFFFFFu;
};



#endif //COLLIDER_H
