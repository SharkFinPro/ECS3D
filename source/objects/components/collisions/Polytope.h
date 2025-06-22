#ifndef POLYTOPE_H
#define POLYTOPE_H

#include <glm/glm.hpp>
#include <array>
#include <memory>
#include <optional>
#include <vector>

class Simplex;
struct SupportVertex;
class Collider;

using Edge = std::pair<uint8_t, uint8_t>;

struct ClosestPoint {
  glm::vec3 point;
  float distance;
};

struct Face {
  std::array<uint8_t, 3> vertices;
  glm::vec3 normal;
  ClosestPoint closestPoint;
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

class Polytope {
public:
  Polytope(Collider* collider, std::shared_ptr<Collider> otherCollider, Simplex& simplex);

  [[nodiscard]] glm::vec3 getMinimumTranslationVector() const;

private:
  Collider* collider;
  std::shared_ptr<Collider> otherCollider;

  std::vector<SupportVertex> vertices;
  std::vector<Face> faces;
  ClosestFaceData closestFaceData{};

  void EPA();

  void generatePolytope(Simplex& simplex);

  float findClosestFace();

  [[nodiscard]] glm::vec3 getSearchDirection() const;

  static bool closeEnough(float minDistance, const std::optional<float>& previousMinDistance,
                          glm::vec3 currentClosestPoint, const std::optional<glm::vec3>& previousClosestPoint);

  std::vector<Edge> deconstructPolytope(glm::vec3 supportPoint, float& currentMinDist);

  bool isFacingInward(const FaceData& faceData);

  void constructFace(Edge edge, glm::vec3 supportPoint, float& currentMinDist);

  void reconstructPolytope(glm::vec3 supportPoint, glm::vec3 direction, float& currentMinDist);

  bool isDuplicateVertex(glm::vec3 supportPoint);
};



#endif //POLYTOPE_H
