#include "Polytope.h"
#include "Simplex.h"
#include "Support.h"
#include <objects/components/collisions/Collider.h>
#include <objects/components/collisions/SphereCollider.h>
#include <objects/components/Transform.h>
#include <objects/Object.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

glm::vec3 closestPointOnPlane(const glm::vec3& a, const glm::vec3& normal)
{
  const auto d = glm::dot(normal, a);

  const auto p = d / glm::dot(normal, normal);

  return normal * p;
}

// World-space box geometry, rebuilt from the same position/scale/rotation BoxCollider::getPosition,
// getScale and getRotation already expose - the exact inputs generateTransformedMesh combines into its
// own transform matrix - so this needs no new accessor on BoxCollider and cannot disagree with whatever
// mesh it has cached.
struct BoxGeometry
{
  glm::vec3 center;
  std::array<glm::vec3, 3> axes;        // world-space unit local X/Y/Z
  std::array<float, 3> halfExtents;     // magnitude along the matching axis above
};

BoxGeometry boxGeometryOf(Collider& collider)
{
  const auto rotation = glm::radians(collider.getRotation());
  const auto scale = collider.getScale();

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
// with the contact normal, leaving its position along that normal axis untouched. A vertex already
// within the footprint comes back unchanged; one overhanging past an edge is pulled back to the nearest
// point on that edge instead of being discarded outright - which matters for two reasons: it gives the
// true overlap's centroid rather than just one box's whole face when a smaller box hangs partway off a
// larger one, and it keeps a contact point at all for a same-size box resting yawed on another, where
// every one of its corners individually pokes outside the other box's own (axis-aligned-to-itself)
// footprint even though the two faces plainly overlap.
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

Polytope::Polytope(Collider& collider, Collider& otherCollider, Simplex &simplex)
  : m_collider(&collider), m_otherCollider(&otherCollider)
{
  generatePolytope(simplex);

  EPA();
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

  // Both remaining colliders are boxes (the sphere cases above already returned). Rather than trust a
  // single EPA support point - which, for a flat face-to-face contact, is just whichever of several
  // tied vertices findFurthestPoint happened to visit first - build the actual contact manifold: the
  // incident box's touching feature (face/edge/vertex), clipped onto the reference box's footprint.
  //
  // Only one side contributes vertices, not both averaged together: for two boxes of very different
  // size this does not matter (the larger box's clipped corners land exactly on the smaller box's own,
  // as folding both in would too), but for a genuine edge or corner contact - one box touching the other
  // at a single real feature - also folding in the flat box's own (large, centred) footprint corners
  // pulls the centroid back toward that box's centre, diluting a lever arm that should not be diluted at
  // all. The reference is whichever box's own axis is more nearly parallel to the contact normal (the
  // flatter, more face-on side); the incident box is the other one, contributing its own real feature.
  const auto boxA = boxGeometryOf(*m_collider);
  const auto boxB = boxGeometryOf(*m_otherCollider);

  const auto normal = glm::normalize(closestPoint);

  const auto axisIndexA = dominantAxisIndex(boxA, normal);
  const auto axisIndexB = dominantAxisIndex(boxB, normal);

  const bool aIsReference = std::fabs(glm::dot(boxA.axes[axisIndexA], normal)) >= std::fabs(glm::dot(boxB.axes[axisIndexB], normal));

  const auto& referenceBox = aIsReference ? boxA : boxB;
  const auto& incidentBox = aIsReference ? boxB : boxA;
  const auto referenceAxisIndex = aIsReference ? axisIndexA : axisIndexB;

  // The side of the incident box's touching feature: the side nearer the reference box's centre, not
  // simply +normal, so this does not depend on which collider EPA happened to call m_collider vs
  // m_otherCollider or on the sign convention of the minimum translation vector.
  const auto towardReference = glm::dot(referenceBox.center - incidentBox.center, normal) >= 0.0f ? normal : -normal;

  const auto incidentVertices = touchingVertices(incidentBox, towardReference);

  // Never empty: touchingVertices always returns at least the one extreme vertex, and clampToFootprint
  // always returns a point (projected onto the reference box's footprint if it overhangs past it,
  // unchanged otherwise) rather than discarding it - so there is no glancing-contact case left that needs
  // a separate fallback.
  glm::vec3 centroid{ 0 };
  for (const auto& vertex : incidentVertices)
  {
    centroid += clampToFootprint(vertex, referenceBox, referenceAxisIndex);
  }

  return centroid / static_cast<float>(incidentVertices.size());
}

void Polytope::EPA()
{
  std::optional<glm::vec3> previousClosestPoint;
  std::optional<float> previousMinDist;

  auto currentMinDist = findClosestFace();

  constexpr uint8_t maxIterations = 25;
  uint8_t iteration = 0;
  while (iteration < maxIterations)
  {
    if (closeEnough(currentMinDist, previousMinDist, m_closestFaceData.closestPoint, previousClosestPoint))
    {
      break;
    }

    const auto searchDirection = glm::normalize(getSearchDirection());

    const auto supportPoint = getSupport(*m_collider, *m_otherCollider, searchDirection);

    if (isDuplicateVertex(supportPoint))
    {
      break;
    }

    previousMinDist = currentMinDist;
    previousClosestPoint = m_closestFaceData.closestPoint;

    currentMinDist = std::numeric_limits<float>::max();
    reconstructPolytope(supportPoint, searchDirection, currentMinDist);

    ++iteration;
  }
}

void Polytope::generatePolytope(Simplex &simplex)
{
  const auto A = simplex.getA();
  const auto B = simplex.getB();
  const auto C = simplex.getC();
  const auto D = simplex.getD();

  const auto AB = B - A;
  const auto AC = C - A;
  const auto AD = D - A;
  const auto BC = C - B;
  const auto BD = D - B;

  auto ABC = glm::cross(AB, AC);
  auto ACD = glm::cross(AC, AD);
  auto ADB = glm::cross(AD, AB);
  auto BCD = glm::cross(BC, BD);

  if (sameDirection(ABC, D))
  {
    ABC *= -1;
  }
  if (sameDirection(ACD, B))
  {
    ACD *= -1;
  }
  if (sameDirection(ADB, C))
  {
    ADB *= -1;
  }
  if (sameDirection(BCD, A))
  {
    BCD *= -1;
  }

  const auto facePointA = closestPointOnPlane(A, ABC);
  const auto facePointB = closestPointOnPlane(A, ACD);
  const auto facePointC = closestPointOnPlane(A, ADB);
  const auto facePointD = closestPointOnPlane(B, BCD);

  m_vertices = {
    simplex.getSupportA(),
    simplex.getSupportB(),
    simplex.getSupportC(),
    simplex.getSupportD()
  };

  m_faces = {{
    {
      .vertices = { 0, 1, 2 },
      .normal = ABC,
      .closestPoint = {
        .point = facePointA,
        .distance = glm::dot(facePointA, facePointA)
      }
    },
    {
      .vertices = { 0, 2, 3 },
      .normal = ACD,
      .closestPoint = {
        .point = facePointB,
        .distance = glm::dot(facePointB, facePointB)
      }
    },
    {
      .vertices = { 0, 1, 3 },
      .normal = ADB,
      .closestPoint = {
        .point = facePointC,
        .distance = glm::dot(facePointC, facePointC)
      }
    },
    {
      .vertices = { 1, 2, 3 },
      .normal = BCD,
      .closestPoint = {
        .point = facePointD,
        .distance = glm::dot(facePointD, facePointD)
      }
    }
  }};
}

float Polytope::findClosestFace()
{
  float minDist = std::numeric_limits<float>::max();

  for (int i = 0; i < m_faces.size(); ++i)
  {
    if (const float dist = m_faces[i].closestPoint.distance; dist < minDist)
    {
      minDist = dist;
      m_closestFaceData.closestPoint = m_faces[i].closestPoint.point;
      m_closestFaceData.closestFaceIndex = i;
    }
  }

  return minDist;
}

glm::vec3 Polytope::getSearchDirection() const
{
  glm::vec3 searchDirection = m_closestFaceData.closestPoint;

  if (dot(searchDirection, searchDirection) < 1e-5f)
  {
    searchDirection = m_faces[m_closestFaceData.closestFaceIndex].normal;
  }

  return searchDirection;
}

bool Polytope::closeEnough(const float minDistance, const std::optional<float>& previousMinDistance,
                           const glm::vec3 currentClosestPoint, const std::optional<glm::vec3>& previousClosestPoint)
{
  constexpr float minDist = 1e-5f;

  if (!previousClosestPoint.has_value())
  {
    return false;
  }

  if (std::fabs(minDistance - previousMinDistance.value()) >= minDist)
  {
    return false;
  }

  if (length(currentClosestPoint) < minDist)
  {
    return false;
  }

  const float deltaX = std::fabs(currentClosestPoint.x - previousClosestPoint.value().x);
  const float deltaY = std::fabs(currentClosestPoint.y - previousClosestPoint.value().y);
  const float deltaZ = std::fabs(currentClosestPoint.z - previousClosestPoint.value().z);

  return deltaX + deltaY + deltaZ < minDist;
}

std::vector<Edge> Polytope::deconstructPolytope(glm::vec3 supportPoint, float& currentMinDist)
{
  std::vector<Edge> edges;

  for (int i = 0; i < m_faces.size();)
  {
    auto [faceVertices, normal, c] = m_faces[i];

    auto facePoint = m_vertices[faceVertices[0]].vertex;

    if (auto vectorToSupportPoint = supportPoint - facePoint; sameDirection(normal, vectorToSupportPoint))
    {
      edges.emplace_back( faceVertices[0], faceVertices[1] );
      edges.emplace_back( faceVertices[1], faceVertices[2] );
      edges.emplace_back( faceVertices[2], faceVertices[0] );

      std::swap(m_faces[i], m_faces.back());
      m_faces.pop_back();

      continue;
    }

    if (const float dist = m_faces[i].closestPoint.distance; dist < currentMinDist)
    {
      currentMinDist = dist;
      m_closestFaceData.closestPoint = m_faces[i].closestPoint.point;
      m_closestFaceData.closestFaceIndex = i;
    }

    ++i;
  }

  std::map<Edge, int> edgeCount;
  for (const auto& edge : edges)
  {
    auto sortedEdge = edge.first < edge.second ? edge : std::make_pair(edge.second, edge.first);

    ++edgeCount[sortedEdge];
  }

  std::vector<Edge> uniqueEdges;
  for (const auto& edge : edges)
  {
    if (auto sortedEdge = edge.first < edge.second ? edge : std::make_pair(edge.second, edge.first); edgeCount[sortedEdge] == 1)
    {
      uniqueEdges.push_back(edge);
    }
  }

  return uniqueEdges;
}

bool Polytope::isFacingInward(const FaceData& faceData) const
{
  for (int i = 0; i < m_vertices.size(); ++i)
  {
    if (i == faceData.aIndex || i == faceData.bIndex)
    {
      continue;
    }

    if (const glm::vec3 faceToVertex = m_vertices[i].vertex - faceData.a; sameDirection(faceData.normal, faceToVertex))
    {
      return true;
    }
  }

  return false;
}

void Polytope::constructFace(Edge edge, glm::vec3 supportPoint, float& currentMinDist)
{
  FaceData faceData {
    .aIndex = edge.first,
    .bIndex = edge.second,
    .a = m_vertices[edge.first].vertex,
    .b = m_vertices[edge.second].vertex,
    .c = supportPoint
  };

  const auto AB = faceData.b - faceData.a;
  const auto AC = faceData.c - faceData.a;

  faceData.normal = cross(AB, AC);

  if (isFacingInward(faceData))
  {
    faceData.normal *= -1;

    if (isFacingInward(faceData))
    {
      return;
    }
  }

  const auto closestPoint = closestPointOnPlane(faceData.a, faceData.normal);
  float distance = glm::dot(closestPoint, closestPoint);

  if (distance < currentMinDist)
  {
    currentMinDist = distance;
    m_closestFaceData.closestPoint = closestPoint;
    m_closestFaceData.closestFaceIndex = m_faces.size();
  }

  m_faces.push_back({
      .vertices = {
        faceData.aIndex,
        faceData.bIndex,
        static_cast<uint8_t>(m_vertices.size())
      },
      .normal = faceData.normal,
      .closestPoint = {
        .point = closestPoint,
        .distance = distance
      }
  });
}

void Polytope::reconstructPolytope(const glm::vec3 supportPoint, const glm::vec3 direction, float& currentMinDist)
{
  for (const auto& edge : deconstructPolytope(supportPoint, currentMinDist))
  {
    constructFace(edge, supportPoint, currentMinDist);
  }

  m_vertices.push_back({supportPoint, direction});
}

bool Polytope::isDuplicateVertex(const glm::vec3 supportPoint)
{
  auto isEqual = [&](const SupportVertex& support) {
    return support.vertex.x == supportPoint.x &&
           support.vertex.y == supportPoint.y &&
           support.vertex.z == supportPoint.z;
  };

  return std::ranges::any_of(m_vertices, isEqual);
}
