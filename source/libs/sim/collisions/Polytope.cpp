#include "Polytope.h"
#include "Simplex.h"
#include "Support.h"
#include "ContactManifold.h"
#include "FaceManifold.h"
#include "ManifoldInternal.h"
#include "NarrowPhase.h"
#include <objects/components/collisions/Collider.h>
#include <objects/components/collisions/CapsuleCollider.h>
#include <objects/components/collisions/ConvexMeshCollider.h>
#include <objects/components/collisions/SphereCollider.h>
#include <objects/components/Transform.h>
#include <objects/Object.h>
#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>

// World-space box geometry, rebuilt from the position/scale/rotation Collider already exposes rather
// than a new accessor onto BoxCollider's own cached mesh.
struct BoxGeometry
{
  glm::vec3 center;
  std::array<glm::vec3, 3> axes;        // world-space unit local X/Y/Z
  std::array<float, 3> halfExtents;     // magnitude along the matching axis above
};

BoxGeometry boxGeometryOf(Collider& collider)
{
  const auto rotation = glm::radians(collider.getRotation());
  const auto scale = glm::abs(collider.getScale());

  const auto rotationMatrix = glm::mat3(
    glm::rotate(glm::mat4(1.0f), rotation.z, { 0, 0, 1 }) *
    glm::rotate(glm::mat4(1.0f), rotation.y, { 0, 1, 0 }) *
    glm::rotate(glm::mat4(1.0f), rotation.x, { 1, 0, 0 }));

  return {
    .center = collider.getPosition(),
    .axes = {
      rotationMatrix * glm::vec3{ 1, 0, 0 },
      rotationMatrix * glm::vec3{ 0, 1, 0 },
      rotationMatrix * glm::vec3{ 0, 0, 1 }
    },
    .halfExtents = { scale.x, scale.y, scale.z }
  };
}

std::array<glm::vec3, 8> boxVerticesOf(const BoxGeometry& box)
{
  std::array<glm::vec3, 8> vertices;

  int i = 0;
  for (const float signX : { -1.0f, 1.0f })
  {
    for (const float signY : { -1.0f, 1.0f })
    {
      for (const float signZ : { -1.0f, 1.0f })
      {
        vertices[i++] = box.center
          + signX * box.halfExtents[0] * box.axes[0]
          + signY * box.halfExtents[1] * box.axes[1]
          + signZ * box.halfExtents[2] * box.axes[2];
      }
    }
  }

  return vertices;
}

// Which of the box's own axes is most nearly parallel to the contact normal - the axis its touching
// face/edge/vertex is stacked along, and so the one to ignore when checking whether another point
// falls within its footprint.
int dominantAxisIndex(const BoxGeometry& box, const glm::vec3& normal)
{
  int best = 0;
  float bestDot = std::fabs(glm::dot(box.axes[0], normal));

  for (int i = 1; i < 3; ++i)
  {
    if (const float d = std::fabs(glm::dot(box.axes[i], normal)); d > bestDot)
    {
      bestDot = d;
      best = i;
    }
  }

  return best;
}

// The box's own vertices nearest the other box along the contact normal - one for a corner contact, two
// for an edge, or a whole face's four when the box rests flush against a flat surface.
std::vector<glm::vec3> touchingVertices(const BoxGeometry& box, const glm::vec3& towardOther)
{
  const auto vertices = boxVerticesOf(box);

  float extreme = std::numeric_limits<float>::lowest();
  for (const auto& vertex : vertices)
  {
    extreme = std::max(extreme, glm::dot(vertex, towardOther));
  }

  const float epsilon = 1e-3f * std::max({ box.halfExtents[0], box.halfExtents[1], box.halfExtents[2], 1.0f });

  std::vector<glm::vec3> selected;
  for (const auto& vertex : vertices)
  {
    if (glm::dot(vertex, towardOther) >= extreme - epsilon)
    {
      selected.push_back(vertex);
    }
  }

  return selected;
}

// Projects a point onto the other box's own extent along its two axes other than the one most aligned
// with the contact normal, leaving its normal-axis position untouched - pulling an overhanging vertex
// back onto the nearest edge instead of discarding it outright.
glm::vec3 clampToFootprint(const glm::vec3& point, const BoxGeometry& box, const int normalAxisIndex)
{
  const auto relative = point - box.center;

  glm::vec3 clamped = box.center;

  for (int i = 0; i < 3; ++i)
  {
    const float coordinate = glm::dot(relative, box.axes[i]);
    const float clampedCoordinate = i == normalAxisIndex ? coordinate : std::clamp(coordinate, -box.halfExtents[i], box.halfExtents[i]);

    clamped += clampedCoordinate * box.axes[i];
  }

  return clamped;
}

// The capsule's two cap points facing the other shape, those within a tolerance of the deepest along that
// direction: both when it lies flat against a surface, one when it stands on an end. Against a box they are
// pulled onto the box's footprint, like a box's touching vertices.
std::vector<glm::vec3> capsuleContactPoints(CapsuleCollider& capsule, Collider& other, const glm::vec3& towardOther)
{
  const auto [start, end] = capsule.getSegment();
  const float radius = capsule.getRadius();

  const std::array<glm::vec3, 2> candidates = { start + towardOther * radius, end + towardOther * radius };
  const float extreme = std::max(glm::dot(candidates[0], towardOther), glm::dot(candidates[1], towardOther));
  // Scaled by the segment so a capsule within about a degree of lying flat still counts as flush, the way
  // touchingVertices treats a box face.
  const float epsilon = 1e-3f * std::max(radius, 1.0f) + 0.02f * glm::distance(start, end);

  std::vector<glm::vec3> points;
  for (const auto& candidate : candidates)
  {
    if (glm::dot(candidate, towardOther) >= extreme - epsilon)
    {
      points.push_back(candidate);
    }
  }

  if (other.getColliderType() == ColliderType::boxCollider)
  {
    const auto box = boxGeometryOf(other);
    const int axisIndex = dominantAxisIndex(box, towardOther);

    for (auto& point : points)
    {
      point = clampToFootprint(point, box, axisIndex);
    }
  }

  return points;
}

// The vertices of a hull or box, in world space.
std::vector<glm::vec3> worldVerticesOf(Collider& collider)
{
  if (collider.getColliderType() == ColliderType::convexMeshCollider)
  {
    return dynamic_cast<ConvexMeshCollider&>(collider).getWorldVertices();
  }

  const auto vertices = boxVerticesOf(boxGeometryOf(collider));

  return std::vector<glm::vec3>(vertices.begin(), vertices.end());
}

// The part of a shape that touches along towardOther: a capsule's cap points, otherwise the extreme vertices.
std::vector<glm::vec3> touchingFeatureOf(Collider& collider, Collider& other, const glm::vec3& towardOther)
{
  if (collider.getColliderType() == ColliderType::capsuleCollider)
  {
    return capsuleContactPoints(dynamic_cast<CapsuleCollider&>(collider), other, towardOther);
  }

  return collisions::touchingFeature(worldVerticesOf(collider), towardOther);
}

// The geometry of a box or hull for collisions::referenceFaceManifold, which views it.
struct OwnedFaces
{
  std::vector<glm::vec3> vertices;
  std::vector<glm::vec3> normals;
  float epsilon;

  [[nodiscard]] collisions::FaceShape view() const
  {
    return { vertices, normals, epsilon };
  }
};

// The touching tolerance scales with the shape's own size along its local axes, so a box-shaped hull selects
// the same vertices a box does at any orientation.
float touchingEpsilon(const glm::vec3& halfExtents)
{
  return 1e-3f * std::max({ halfExtents.x, halfExtents.y, halfExtents.z, 1.0f });
}

OwnedFaces facesOf(Collider& collider)
{
  if (collider.getColliderType() == ColliderType::convexMeshCollider)
  {
    auto& hullCollider = dynamic_cast<ConvexMeshCollider&>(collider);
    const auto& hull = hullCollider.getHull();

    glm::vec3 low = hull.vertices.front();
    glm::vec3 high = low;
    for (const auto& vertex : hull.vertices)
    {
      low = glm::min(low, vertex);
      high = glm::max(high, vertex);
    }

    OwnedFaces faces{ hullCollider.getWorldVertices(), {},
                      touchingEpsilon((high - low) * 0.5f * glm::abs(collider.getScale())) };
    faces.normals = collisions::hullFaceNormals(faces.vertices, hull.indices);

    return faces;
  }

  const auto box = boxGeometryOf(collider);
  const auto vertices = boxVerticesOf(box);

  OwnedFaces faces{ std::vector<glm::vec3>(vertices.begin(), vertices.end()), {},
                    touchingEpsilon({ box.halfExtents[0], box.halfExtents[1], box.halfExtents[2] }) };
  for (const auto& axis : box.axes)
  {
    faces.normals.push_back(axis);
    faces.normals.push_back(-axis);
  }

  return faces;
}

// Contact::points holds this many.
constexpr size_t maxManifoldPoints = std::tuple_size_v<decltype(collisions::Contact::points)>;

Polytope::Polytope(Collider& collider, Collider& otherCollider, Simplex &simplex)
  : m_collider(&collider), m_otherCollider(&otherCollider)
{
  EPA(simplex);
}

glm::vec3 Polytope::getMinimumTranslationVector() const
{
  return m_closestFaceData.closestPoint;
}

glm::vec3 Polytope::findCollisionPoint() const
{
  const auto transform = m_collider->getOwner()->getComponent<Transform>(ComponentType::transform);
  const auto otherTransform = m_otherCollider->getOwner()->getComponent<Transform>(ComponentType::transform);
  if (!transform || !otherTransform)
  {
    throw std::runtime_error("Collider::EPA::Missing Transform");
  }

  auto closestPoint = m_closestFaceData.closestPoint;

  glm::vec3 pointOfCollision;

  if (m_collider->getColliderType() == ColliderType::sphereCollider)
  {
    auto direction = glm::normalize(closestPoint);

    pointOfCollision = transform->getPosition() + direction * dynamic_cast<SphereCollider*>(m_collider)->getRadius();

    return pointOfCollision;
  }

  if (m_otherCollider->getColliderType() == ColliderType::sphereCollider)
  {
    // closestPoint is a Minkowski-difference point, so its direction runs from m_collider toward
    // m_otherCollider - the opposite of the case above, where the sphere was m_collider itself. Adding
    // it here would land on the far pole of the sphere, away from the other shape; subtracting it lands
    // on the near pole, facing the other shape and inside the overlap.
    auto direction = glm::normalize(closestPoint);

    pointOfCollision = otherTransform->getPosition() - direction * dynamic_cast<SphereCollider*>(m_otherCollider)->getRadius();

    return pointOfCollision;
  }

  glm::vec3 centroid{ 0 };
  const auto points = shapeContactPoints();
  for (const auto& point : points)
  {
    centroid += point;
  }

  // Never empty (each feature always yields at least one point), so no fallback case is needed.
  return centroid / static_cast<float>(points.size());
}

std::vector<glm::vec3> Polytope::findContactManifold() const
{
  if (m_collider->getColliderType() == ColliderType::sphereCollider ||
      m_otherCollider->getColliderType() == ColliderType::sphereCollider)
  {
    return { findCollisionPoint() };
  }

  return shapeContactPoints();
}

std::vector<glm::vec3> Polytope::shapeContactPoints() const
{
  if (isHullPair())
  {
    return hullContactPoints();
  }

  return distinctPoints(isCapsulePair() ? capsuleFeaturePoints() : boxContactPoints());
}

bool Polytope::isHullPair() const
{
  return m_collider->getColliderType() == ColliderType::convexMeshCollider ||
         m_otherCollider->getColliderType() == ColliderType::convexMeshCollider;
}

std::vector<glm::vec3> Polytope::hullContactPoints() const
{
  // closestPoint runs from m_collider toward m_otherCollider, so the first shape touches along the normal
  // and the second along its reverse.
  const auto normal = glm::normalize(m_closestFaceData.closestPoint);

  if (!isCapsulePair())
  {
    const auto first = facesOf(*m_collider);
    const auto second = facesOf(*m_otherCollider);

    if (auto points = collisions::referenceFaceManifold(first.view(), second.view(), normal, maxManifoldPoints);
        !points.empty())
    {
      return points;
    }
  }

  return collisions::clipManifold(touchingFeatureOf(*m_collider, *m_otherCollider, normal),
                                  touchingFeatureOf(*m_otherCollider, *m_collider, -normal), normal,
                                  maxManifoldPoints);
}

bool Polytope::isCapsulePair() const
{
  return m_collider->getColliderType() == ColliderType::capsuleCollider ||
         m_otherCollider->getColliderType() == ColliderType::capsuleCollider;
}

std::vector<glm::vec3> Polytope::capsuleFeaturePoints() const
{
  // closestPoint runs from m_collider toward m_otherCollider (see findCollisionPoint), so the direction
  // toward the other shape is its normal when the capsule is m_collider and the reverse when it is not.
  const auto normal = glm::normalize(m_closestFaceData.closestPoint);

  if (m_collider->getColliderType() == ColliderType::capsuleCollider)
  {
    return capsuleContactPoints(dynamic_cast<CapsuleCollider&>(*m_collider), *m_otherCollider, normal);
  }

  return capsuleContactPoints(dynamic_cast<CapsuleCollider&>(*m_otherCollider), *m_collider, -normal);
}

std::vector<glm::vec3> Polytope::distinctPoints(const std::vector<glm::vec3>& points)
{
  std::vector<glm::vec3> manifold;
  for (const auto& point : points)
  {
    const bool duplicate = std::ranges::any_of(manifold, [&point](const glm::vec3& existing)
    {
      return glm::distance(existing, point) < 1e-4f;
    });

    if (!duplicate)
    {
      manifold.push_back(point);
    }
  }

  return manifold;
}

std::vector<glm::vec3> Polytope::boxContactPoints() const
{
  const auto closestPoint = m_closestFaceData.closestPoint;

  // Both colliders are boxes here. Build the contact manifold from the incident box's touching feature clipped onto the reference box's footprint - the
  // flatter side (its axis more nearly parallel to the normal) is the reference, so a real edge/corner
  // contact is not diluted by also folding in the flat side's own footprint corners.
  const auto boxA = boxGeometryOf(*m_collider);
  const auto boxB = boxGeometryOf(*m_otherCollider);

  const auto normal = glm::normalize(closestPoint);

  const auto axisIndexA = dominantAxisIndex(boxA, normal);
  const auto axisIndexB = dominantAxisIndex(boxB, normal);

  const bool aIsReference = std::fabs(glm::dot(boxA.axes[axisIndexA], normal)) >=
    std::fabs(glm::dot(boxB.axes[axisIndexB], normal)) - collisions::faceAlignmentTie;

  const auto& referenceBox = aIsReference ? boxA : boxB;
  const auto& incidentBox = aIsReference ? boxB : boxA;
  const auto referenceAxisIndex = aIsReference ? axisIndexA : axisIndexB;

  // The side nearer the reference box's centre, regardless of the minimum translation vector's sign.
  const auto towardReference = glm::dot(referenceBox.center - incidentBox.center, normal) >= 0.0f ? normal : -normal;

  const auto incidentVertices = touchingVertices(incidentBox, towardReference);

  std::vector<collisions::detail::Candidate> candidates;
  std::vector<glm::vec3> placed;
  const auto add = [&candidates, &placed](const glm::vec3& point)
  {
    if (!collisions::detail::containsNear(placed, point))
    {
      placed.push_back(point);
      candidates.push_back({ point, 0.0f });
    }
  };

  for (const auto& vertex : incidentVertices)
  {
    add(clampToFootprint(vertex, referenceBox, referenceAxisIndex));
  }

  // Clamping alone finds the overlap only while the incident face lies inside the reference one. A face larger
  // than the reference, or turned against it, clamps its corners onto as few as two of the reference's, so the
  // reference corners that lie over the incident face belong to the overlap too, taken at the incident face's depth
  // like the clamped corners. An edge or corner has no face to lie over.
  constexpr size_t faceCorners = 4;
  const auto incidentAxisIndex = aIsReference ? axisIndexB : axisIndexA;
  const auto referenceVertices = incidentVertices.size() == faceCorners
                                   ? touchingVertices(referenceBox, -towardReference)
                                   : std::vector<glm::vec3>{};
  for (const auto& vertex : referenceVertices)
  {
    if (glm::distance(clampToFootprint(vertex, incidentBox, incidentAxisIndex), vertex) < collisions::detail::dedupeDistance)
    {
      add(vertex + glm::dot(incidentVertices.front() - vertex, normal) * normal);
    }
  }

  return collisions::detail::spreadOut(candidates, maxManifoldPoints);
}

namespace {
  // The polytope's support points are floats, so where the shapes' boundary lies is known only to a few float ulps
  // of the coordinates involved. Converging any tighter chases rounding; any looser and a resting overlap, which is
  // itself only that deep, is lost in the tolerance.
  constexpr double convergenceUlps = 8.0;

  // A support point beyond a face by less than this is on it. Below the convergence band, so the closest face is
  // always visible from a support point that has not converged.
  constexpr double visibilityUlps = 2.0;

  // A tetrahedron flatter than this share of its size cubed has no inside to expand from.
  constexpr double flatVolumeShare = 1e-6;

  // A face whose doubled area is below this share of its two edges' lengths multiplied has no reliable normal.
  constexpr double slivery = 1e-12;

  constexpr double floatEpsilon = std::numeric_limits<float>::epsilon();

  // Converging that tightly takes a curved shape more steps than a flat one; past this the best translation
  // measured so far stands.
  constexpr int maxEpaIterations = 64;

  [[nodiscard]] double ulpScale(const glm::dvec3& point)
  {
    return floatEpsilon * std::max(1.0, glm::length(point));
  }
}

void Polytope::EPA(const Simplex& simplex)
{
  m_closestFaceData.closestPoint = glm::vec3(0.0f);

  if (!buildInitialPolytope(simplex))
  {
    return;
  }

  // Every support point measured below is a translation that separates the shapes along its direction, and its
  // length bounds the depth from above; the closest face's distance bounds it from below. The answer is the best
  // measured translation, so whatever stops the loop, it never reports a face from inside the overlap.
  Measured best;
  bool converged = false;

  for (int iteration = 0; iteration < maxEpaIterations && !m_faces.empty(); ++iteration)
  {
    const auto& closest = *std::ranges::min_element(m_faces, {}, &EpaFace::distance);
    const double lowerBound = closest.distance;
    const auto measured = measure(closest.normal);
    best = std::min(best, measured, [](const Measured& a, const Measured& b) { return a.overlap < b.overlap; });

    if (measured.overlap - lowerBound <= measured.resolution)
    {
      converged = true;
      break;
    }

    if (isKnownVertex(measured.support) || !expandToward(measured.support))
    {
      break;
    }
  }

  // Stopped short of converging: the polytope still bounds the overlap, and the true direction out is nearest one of
  // its face normals, so the best of them all stands rather than the one direction that happened to be measured.
  if (!converged)
  {
    for (const auto& face : m_faces)
    {
      best = std::min(best, measure(face.normal), [](const Measured& a, const Measured& b) {
        return a.overlap < b.overlap;
      });
    }
  }

  // An overlap within the band the boundary is known to is indistinguishable from the shapes only touching.
  if (best.overlap > best.resolution && best.overlap != std::numeric_limits<double>::max())
  {
    m_closestFaceData.closestPoint = glm::vec3(best.normal * best.overlap);
  }
}

Polytope::Measured Polytope::measure(const glm::dvec3& normal) const
{
  const glm::dvec3 support(getSupport(*m_collider, *m_otherCollider, glm::vec3(normal)));

  return { normal, support, glm::dot(normal, support), convergenceUlps * ulpScale(support) };
}

std::optional<Polytope::EpaFace> Polytope::faceOf(const uint8_t a, const uint8_t b, const uint8_t c) const
{
  const auto& pointA = m_vertices[a];
  const auto first = m_vertices[b] - pointA;
  const auto second = m_vertices[c] - pointA;
  const auto crossed = glm::cross(first, second);
  const double length = glm::length(crossed);

  if (!(length > slivery * glm::length(first) * glm::length(second)))
  {
    return std::nullopt;
  }

  const auto normal = crossed / length;

  return EpaFace{ { a, b, c }, normal, glm::dot(normal, pointA) };
}

bool Polytope::addFace(const uint8_t a, const uint8_t b, const uint8_t c)
{
  auto face = faceOf(a, b, c);
  if (!face)
  {
    return false;
  }

  // Wound so the normal points out, away from a point inside every polytope this one grows into.
  if (glm::dot(face->normal, m_interior - m_vertices[a]) > 0.0)
  {
    face = faceOf(a, c, b);
  }

  m_faces.push_back(*face);

  return true;
}

bool Polytope::buildInitialPolytope(const Simplex& simplex)
{
  m_vertices = { glm::dvec3(simplex.getA()), glm::dvec3(simplex.getB()), glm::dvec3(simplex.getC()),
                 glm::dvec3(simplex.getD()) };

  const auto& a = m_vertices[0];
  double size = 0.0;
  for (size_t i = 0; i < m_vertices.size(); ++i)
  {
    for (size_t j = i + 1; j < m_vertices.size(); ++j)
    {
      size = std::max(size, glm::distance(m_vertices[i], m_vertices[j]));
    }
  }

  const double volume = std::abs(glm::dot(m_vertices[1] - a, glm::cross(m_vertices[2] - a, m_vertices[3] - a)));
  if (!(volume > flatVolumeShare * size * size * size))
  {
    return buildAroundFlatSimplex();
  }

  m_interior = (m_vertices[0] + m_vertices[1] + m_vertices[2] + m_vertices[3]) * 0.25;

  m_faces.clear();
  return addFace(0, 1, 2) && addFace(0, 2, 3) && addFace(0, 3, 1) && addFace(1, 3, 2);
}

bool Polytope::buildAroundFlatSimplex()
{
  // GJK ends on a flat tetrahedron when the origin lies on the overlap's boundary or the shapes only just touch.
  // Its four triangles cover its flat hull, so one of them holds the origin's projection onto its plane, and the
  // polytope grows from that triangle. When none does, the origin is outside the flat hull and GJK took rounding
  // for an enclosed origin, so there is no overlap to measure.
  constexpr std::array<std::array<uint8_t, 3>, 4> triangles = { { { 0, 1, 2 }, { 0, 1, 3 }, { 0, 2, 3 }, { 1, 2, 3 } } };

  std::optional<std::array<uint8_t, 3>> chosen;
  for (const auto& triangle : triangles)
  {
    const auto& p = m_vertices[triangle[0]];
    const auto crossed = glm::cross(m_vertices[triangle[1]] - p, m_vertices[triangle[2]] - p);
    const double area = glm::length(crossed);
    if (!(area > 0.0))
    {
      continue;
    }

    const auto normal = crossed / area;
    const auto projected = normal * glm::dot(normal, p);
    bool holdsOrigin = true;
    for (int edge = 0; edge < 3; ++edge)
    {
      const auto& from = m_vertices[triangle[edge]];
      const auto& to = m_vertices[triangle[(edge + 1) % 3]];
      // A little slack, so the origin on an edge two triangles share counts for both.
      holdsOrigin = holdsOrigin && glm::dot(glm::cross(to - from, projected - from), normal) >= -1e-9 * area;
    }

    if (holdsOrigin)
    {
      chosen = triangle;
      break;
    }
  }

  if (!chosen)
  {
    return false;
  }

  const auto base = *chosen;
  const auto& p = m_vertices[base[0]];
  const auto normal = glm::normalize(glm::cross(m_vertices[base[1]] - p, m_vertices[base[2]] - p));

  const glm::dvec3 above(getSupport(*m_collider, *m_otherCollider, glm::vec3(normal)));
  const glm::dvec3 below(getSupport(*m_collider, *m_otherCollider, glm::vec3(-normal)));

  // A side that does not rise off the plane is the overlap's own boundary there: the origin is on it, so the shapes
  // only touch and there is no depth to report.
  if (!(glm::dot(normal, above - p) > convergenceUlps * ulpScale(above)) ||
      !(glm::dot(-normal, below - p) > convergenceUlps * ulpScale(below)))
  {
    return false;
  }

  // The base and the point above it: a tetrahedron, so convex, with the origin on its base. Expansion finds the side
  // below. Both points together are not a start, since their segment need not pass through the base and the
  // bipyramid they make need not be convex.
  m_vertices = { m_vertices[base[0]], m_vertices[base[1]], m_vertices[base[2]], above };
  m_interior = (m_vertices[0] + m_vertices[1] + m_vertices[2] + above) * 0.25;

  m_faces.clear();
  return addFace(0, 1, 2) && addFace(0, 1, 3) && addFace(1, 2, 3) && addFace(2, 0, 3);
}

bool Polytope::isKnownVertex(const glm::dvec3& point) const
{
  return std::ranges::any_of(m_vertices, [&point](const glm::dvec3& vertex) { return vertex == point; });
}

bool Polytope::expandToward(const glm::dvec3& support)
{
  if (m_vertices.size() >= std::numeric_limits<uint8_t>::max())
  {
    return false;
  }

  const double visibility = visibilityUlps * ulpScale(support);

  // Every face the support point is beyond goes; the horizon is each of their directed edges whose reverse belongs
  // to no removed face. Taken in the removed faces' winding, a horizon edge and the support point make a face wound
  // outward like the one it replaces.
  thread_local std::vector<std::pair<uint8_t, uint8_t>> edges;
  edges.clear();

  for (size_t i = 0; i < m_faces.size();)
  {
    const auto& face = m_faces[i];
    if (glm::dot(face.normal, support - m_vertices[face.vertices[0]]) > visibility)
    {
      for (int edge = 0; edge < 3; ++edge)
      {
        edges.emplace_back(face.vertices[edge], face.vertices[(edge + 1) % 3]);
      }

      m_faces[i] = m_faces.back();
      m_faces.pop_back();
      continue;
    }

    ++i;
  }

  if (edges.empty())
  {
    return false;
  }

  const auto supportIndex = static_cast<uint8_t>(m_vertices.size());
  m_vertices.push_back(support);

  for (const auto& edge : edges)
  {
    const auto from = edge.first;
    const auto to = edge.second;
    const bool shared = std::ranges::any_of(edges, [from, to](const auto& other) {
      return other.first == to && other.second == from;
    });

    if (shared)
    {
      continue;
    }

    auto face = faceOf(from, to, supportIndex);
    if (!face || glm::dot(face->normal, m_interior - m_vertices[from]) > 0.0)
    {
      return false;
    }

    m_faces.push_back(*face);
  }

  return true;
}
