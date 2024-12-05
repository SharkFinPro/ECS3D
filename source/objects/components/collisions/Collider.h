#ifndef COLLIDER_H
#define COLLIDER_H

#include "../Component.h"
#include <memory>
#include <glm/vec3.hpp>
#include <array>
#include <vector>
#include "Simplex.h"
#include <optional>

class Transform;

struct ClosestPoint {
  glm::vec3 point;
  float distance;
};

struct Face {
  std::array<int, 3> vertices;
  glm::vec3 normal;
  ClosestPoint closestPoint;
};

struct Polytope {
  std::vector<glm::vec3> vertices;
  std::vector<Face> faces;
};

struct ClosestFaceData {
  glm::vec3 closestPoint;
  int closestFaceIndex;
};

struct FaceData {
  int aIndex;
  int bIndex;
  glm::vec3 a;
  glm::vec3 b;
  glm::vec3 c;
  glm::vec3 normal;
};

using Edge = std::pair<int, int>;

enum class ColliderType {
  boxCollider,
  sphereCollider
};

class Collider : public Component {
public:
  explicit Collider(ColliderType type);

  bool collidesWith(const std::shared_ptr<Object>& other, glm::vec3* mtv);

private:
  bool handleSphereToSphereCollision(const std::shared_ptr<Collider>& otherCollider,
                                     const std::shared_ptr<Transform>& otherTransform,
                                     glm::vec3* mtv);

  glm::vec3 getSupport(const std::shared_ptr<Collider>& other, const glm::vec3& direction);
  virtual glm::vec3 findFurthestPoint(const glm::vec3& direction) = 0;

  static bool expandSimplex(Simplex& simplex, glm::vec3& direction);
  static bool lineCase(const Simplex& simplex, glm::vec3& direction);
  static bool triangleCase(Simplex& simplex, glm::vec3& direction);
  static bool tetrahedronCase(Simplex& simplex, glm::vec3& direction);

  static Polytope generatePolytope(Simplex& simplex);

  static glm::vec3 closestPointOnPlane(const glm::vec3& a, const glm::vec3& normal);

  glm::vec3 EPA(Polytope& polytope, const std::shared_ptr<Object>& other);

  static float findClosestFace(ClosestFaceData& closestFaceData, const Polytope& polytope);

  static bool closeEnough(float minDistance, const std::optional<float>& previousMinDistance,
                          glm::vec3 currentClosestPoint, const std::optional<glm::vec3>& previousClosestPoint);

  static glm::vec3 getSearchDirection(const ClosestFaceData& closestFaceData, const Polytope& polytope);

  static std::vector<Edge> deconstructPolytope(glm::vec3 supportPoint, Polytope& polytope);

  static bool isFacingInward(const FaceData& faceData, const Polytope& polytope);

  static void constructFace(Edge edge, glm::vec3 supportPoint, Polytope& polytope);

  void reconstructPolytope(glm::vec3 supportPoint, Polytope& polytope);

  static bool isDuplicateVertex(glm::vec3 supportPoint, const Polytope& polytope);

protected:
  std::weak_ptr<Transform> transform_ptr;

  ColliderType colliderType;
};



#endif //COLLIDER_H
