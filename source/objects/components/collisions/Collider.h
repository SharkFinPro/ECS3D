#ifndef COLLIDER_H
#define COLLIDER_H

// Enable to draw collision bounding box lines
// #define COLLISION_BBOX_DEBUG

#include "../Component.h"
#include <memory>
#include <glm/vec3.hpp>
#include "Simplex.h"

struct Line {
  glm::vec3 start;
  glm::vec3 end;
};

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

class Collider : public Component {
public:
  explicit Collider(ColliderType type, ComponentType subType);

  bool collidesWith(const std::shared_ptr<Object>& other, glm::vec3* mtv, glm::vec3* collisionPoint);

  const BoundingBox& getBoundingBox();

#ifdef COLLISION_BBOX_DEBUG
  void fixedUpdate(float dt) override;

  void variableUpdate(float dt) override;
#endif

  glm::vec3 getSupport(const std::shared_ptr<Collider>& other, const glm::vec3& direction);

  [[nodiscard]] ColliderType getColliderType() const;

  virtual glm::vec3 findFurthestPoint(const glm::vec3& direction) = 0;

private:
#ifdef COLLISION_BBOX_DEBUG
  std::vector<Line> linesToDraw;
#endif

  bool handleSphereToSphereCollision(const std::shared_ptr<Collider>& otherCollider,
                                     const std::shared_ptr<Transform>& otherTransform,
                                     glm::vec3* mtv,
                                     glm::vec3* collisionPoint);

  static bool expandSimplex(Simplex& simplex, glm::vec3& direction);
  static void lineCase(const Simplex& simplex, glm::vec3& direction);
  static void triangleCase(Simplex& simplex, glm::vec3& direction);
  static bool tetrahedronCase(Simplex& simplex, glm::vec3& direction);

protected:
  std::weak_ptr<Transform> transform_ptr;

  ColliderType colliderType;

  BoundingBox boundingBox;

  float roughMaxDistance;
};



#endif //COLLIDER_H
