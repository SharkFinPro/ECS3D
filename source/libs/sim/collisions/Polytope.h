#ifndef POLYTOPE_H
#define POLYTOPE_H

#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

class Simplex;
class Collider;
class SupportShape;

struct ClosestFaceData {
  // The translation EPA found, in the Minkowski difference: its direction runs from the first collider toward the
  // second and its length is how far they overlap along it. Zero when they only touch.
  glm::vec3 closestPoint;
};

class Polytope {
public:
  // Both colliders and their support shapes by reference: a Polytope is built and consumed inside one
  // narrow-phase call, so the caller's own shares outlive it and there is nothing here to own.
  Polytope(Collider& collider, Collider& otherCollider, const SupportShape& shape, const SupportShape& otherShape,
           Simplex& simplex);

  // Returns the vertex and face buffers to a per-thread pool, so the next polytope on the thread grows into
  // capacity that is already there instead of allocating.
  ~Polytope();

  Polytope(const Polytope&) = delete;
  Polytope& operator=(const Polytope&) = delete;

  [[nodiscard]] glm::vec3 getMinimumTranslationVector() const;

  struct ContactPoints {
    // A point on the overlap, in world space.
    glm::vec3 point;

    // Up to four distinct world-space points spanning the contact: the incident face's vertices clamped into
    // the reference face on a box or hull pair, or a capsule's cap points facing the other shape. Empty when
    // only point applies, as against a sphere.
    std::vector<glm::vec3> manifold;
  };

  [[nodiscard]] ContactPoints findContactPoints() const;

private:
  // Where a sphere in the pair touches the other shape.
  [[nodiscard]] glm::vec3 sphereContactPoint() const;

  [[nodiscard]] bool hasSphere() const;

  [[nodiscard]] std::vector<glm::vec3> boxContactPoints() const;

  [[nodiscard]] bool isCapsulePair() const;

  [[nodiscard]] bool isHullPair() const;

  [[nodiscard]] std::vector<glm::vec3> hullContactPoints() const;

  // The manifold of whichever shapes the pair holds, never empty.
  [[nodiscard]] std::vector<glm::vec3> shapeContactPoints() const;

  [[nodiscard]] std::vector<glm::vec3> capsuleFeaturePoints() const;

  [[nodiscard]] static std::vector<glm::vec3> distinctPoints(const std::vector<glm::vec3>& points);

  Collider* m_collider;
  Collider* m_otherCollider;
  const SupportShape* m_shape;
  const SupportShape* m_otherShape;

  // A face of the expanding polytope, wound counter-clockwise seen from outside, with its unit outward normal and
  // its plane's distance from the origin. Double precision, so the polytope's own rounding stays far below the
  // float support points it is built from.
  struct EpaFace {
    std::array<uint8_t, 3> vertices;
    glm::dvec3 normal;
    double distance;
  };

  struct BufferPool {
    std::vector<glm::dvec3> vertices;
    std::vector<EpaFace> faces;
  };

  [[nodiscard]] static BufferPool& bufferPool();

  std::vector<glm::dvec3> m_vertices;
  std::vector<EpaFace> m_faces;

  // Inside every polytope the expansion passes through, which is what a new face is wound away from.
  glm::dvec3 m_interior{ 0.0 };

  ClosestFaceData m_closestFaceData{};

  // The support point along a unit direction and how far the shapes overlap along it, with the band that overlap is
  // known to.
  struct Measured {
    glm::dvec3 normal{ 0.0 };
    glm::dvec3 support{ 0.0 };
    double overlap = std::numeric_limits<double>::max();
    double resolution = 0.0;
  };

  void EPA(const Simplex& simplex);

  [[nodiscard]] Measured measure(const glm::dvec3& normal) const;

  [[nodiscard]] bool buildInitialPolytope(const Simplex& simplex);

  [[nodiscard]] bool buildAroundFlatSimplex();

  [[nodiscard]] std::optional<EpaFace> faceOf(uint8_t a, uint8_t b, uint8_t c) const;

  [[nodiscard]] bool addFace(uint8_t a, uint8_t b, uint8_t c);

  [[nodiscard]] bool isKnownVertex(const glm::dvec3& point) const;

  // Replaces the faces support is beyond with faces from their horizon to it. False when the polytope cannot grow
  // there: nothing is visible, a new face has no reliable normal or would face inward.
  [[nodiscard]] bool expandToward(const glm::dvec3& support);
};



#endif //POLYTOPE_H
