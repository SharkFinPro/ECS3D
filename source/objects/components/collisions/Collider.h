#ifndef COLLIDER_H
#define COLLIDER_H

// Enable to draw collision bounding box lines
#define COLLISION_BBOX_DEBUG

#include "../Component.h"
#include <memory>
#include <glm/vec3.hpp>
#include <array>
#include <vector>
#include "Simplex.h"
#include <optional>

struct Line {
  glm::vec3 start;
  glm::vec3 end;
};

class Transform;

struct ClosestPoint {
  glm::vec3 point;
  float distance;
};

struct Face {
  std::array<uint8_t, 3> vertices;
  glm::vec3 normal;
  ClosestPoint closestPoint;
};

struct Polytope {
  std::vector<SupportVertex> vertices;
  std::vector<Face> faces;
};

struct ClosestFaceData {
  glm::vec3 closestPoint;
  uint8_t closestFaceIndex;
};

struct FaceData {
  uint8_t aIndex;
  uint8_t bIndex;
  glm::vec3 a;
  glm::vec3 b;
  glm::vec3 c;
  glm::vec3 normal;
};

using Edge = std::pair<uint8_t, uint8_t>;

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

  bool collidesWith(const std::shared_ptr<Object>& other, glm::vec3* mtv);

  const BoundingBox& getBoundingBox();

#ifdef COLLISION_BBOX_DEBUG
  void fixedUpdate(float dt) override;

  void variableUpdate(float dt) override;
#endif

private:

  std::vector<Line> linesToDraw;

  bool handleSphereToSphereCollision(const std::shared_ptr<Collider>& otherCollider,
                                     const std::shared_ptr<Transform>& otherTransform,
                                     glm::vec3* mtv);

  glm::vec3 getSupport(const std::shared_ptr<Collider>& other, const glm::vec3& direction);

  virtual glm::vec3 findFurthestPoint(const glm::vec3& direction) = 0;

  static bool expandSimplex(Simplex& simplex, glm::vec3& direction);
  static void lineCase(const Simplex& simplex, glm::vec3& direction);
  static void triangleCase(Simplex& simplex, glm::vec3& direction);
  static bool tetrahedronCase(Simplex& simplex, glm::vec3& direction);

  static Polytope generatePolytope(Simplex& simplex);

  static glm::vec3 closestPointOnPlane(const glm::vec3& a, const glm::vec3& normal);

  glm::vec3 EPA(Polytope& polytope, const std::shared_ptr<Object>& other);

  static float findClosestFace(ClosestFaceData& closestFaceData, const Polytope& polytope);

  static bool closeEnough(float minDistance, const std::optional<float>& previousMinDistance,
                          glm::vec3 currentClosestPoint, const std::optional<glm::vec3>& previousClosestPoint);

  static glm::vec3 getSearchDirection(const ClosestFaceData& closestFaceData, const Polytope& polytope);

  static std::vector<Edge> deconstructPolytope(glm::vec3 supportPoint, Polytope& polytope, float& currentMinDist,
                                               ClosestFaceData& closestFaceData);

  static bool isFacingInward(const FaceData& faceData, const Polytope& polytope);

  static void constructFace(Edge edge, glm::vec3 supportPoint, Polytope& polytope, float& currentMinDist,
                            ClosestFaceData& closestFaceData);

  static void reconstructPolytope(glm::vec3 supportPoint, glm::vec3 direction, Polytope& polytope, float& currentMinDist,
                                  ClosestFaceData& closestFaceData);

  static bool isDuplicateVertex(glm::vec3 supportPoint, const Polytope& polytope);

  glm::vec3 findCollisionPoint(const Polytope& polytope, const std::shared_ptr<Object>& other, const ClosestFaceData& closestFaceData);

protected:
  std::weak_ptr<Transform> transform_ptr;

  ColliderType colliderType;

  BoundingBox boundingBox;

  float roughMaxDistance;
};



#endif //COLLIDER_H
