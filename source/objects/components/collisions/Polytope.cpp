#include "Polytope.h"
#include "Collider.h"
#include "Simplex.h"
#include "SphereCollider.h"
#include "../Transform.h"
#include "../../Object.h"
#include <algorithm>
#include <map>
#include <stdexcept>
#include <utility>

glm::vec3 closestPointOnPlane(const glm::vec3& a, const glm::vec3& normal)
{
  const auto d = glm::dot(normal, a);

  const auto p = d / glm::dot(normal, normal);

  return normal * p;
}

glm::vec3 computeBarycentric(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& p)
{
  const glm::vec3 v0 = c - a;
  const glm::vec3 v1 = b - a;
  const glm::vec3 v2 = p - a;

  const float dot00 = glm::dot(v0, v0);
  const float dot01 = glm::dot(v0, v1);
  const float dot02 = glm::dot(v0, v2);
  const float dot11 = glm::dot(v1, v1);
  const float dot12 = glm::dot(v1, v2);

  const float invDenom = 1.0f / (dot00 * dot11 - dot01 * dot01);
  const float u = (dot11 * dot02 - dot01 * dot12) * invDenom;
  const float v = (dot00 * dot12 - dot01 * dot02) * invDenom;
  const float w = 1.0f - u - v;

  return { u, v, w };
}

glm::vec3 closestPointOnTriangleToOrigin(glm::vec3 a, glm::vec3 b, glm::vec3 c)
{
  // Edge vectors
  glm::vec3 ab = b - a;
  glm::vec3 ac = c - a;
  glm::vec3 ap = -a; // Vector from A to origin (P = origin = (0,0,0))

  // Compute dot products
  float d1 = glm::dot(ab, ap);
  float d2 = glm::dot(ac, ap);

  // Check if P is in vertex region outside A
  if (d1 <= 0.0f && d2 <= 0.0f)
  {
    return a; // Barycentric coordinates (1,0,0)
  }

  // Check if P is in vertex region outside B
  glm::vec3 bp = -b;
  float d3 = glm::dot(ab, bp);
  float d4 = glm::dot(ac, bp);
  if (d3 >= 0.0f && d4 <= d3)
  {
    return b; // Barycentric coordinates (0,1,0)
  }

  // Check if P is in edge region of AB
  float vc = d1 * d4 - d3 * d2;
  if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
  {
    float v = d1 / (d1 - d3);
    return a + v * ab; // Barycentric coordinates (1-v,v,0)
  }

  // Check if P is in vertex region outside C
  glm::vec3 cp = -c;
  float d5 = glm::dot(ab, cp);
  float d6 = glm::dot(ac, cp);
  if (d6 >= 0.0f && d5 <= d6)
  {
    return c; // Barycentric coordinates (0,0,1)
  }

  // Check if P is in edge region of AC
  float vb = d5 * d2 - d1 * d6;
  if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
  {
    float w = d2 / (d2 - d6);
    return a + w * ac; // Barycentric coordinates (1-w,0,w)
  }

  // Check if P is in edge region of BC
  float va = d3 * d6 - d5 * d4;
  if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
  {
    float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
    return b + w * (c - b); // Barycentric coordinates (0,1-w,w)
  }

  // P is inside face region. Compute projection onto triangle plane
  float denom = 1.0f / (va + vb + vc);
  float v = vb * denom;
  float w = vc * denom;
  return a + ab * v + ac * w; // Barycentric coordinates (1-v-w,v,w)
}

Polytope::Polytope(Collider* collider, std::shared_ptr<Collider> otherCollider, Simplex &simplex)
  : collider(collider), otherCollider(std::move(otherCollider))
{
  generatePolytope(simplex);

  EPA();
}

glm::vec3 Polytope::getMinimumTranslationVector() const
{
  return closestFaceData.closestPoint;
}

glm::vec3 Polytope::findCollisionPoint() const
{
  const auto transform = collider->getOwner()->getComponent<Transform>(ComponentType::transform);
  const auto otherTransform = otherCollider->getOwner()->getComponent<Transform>(ComponentType::transform);
  if (!transform || !otherTransform)
  {
    throw std::runtime_error("Collider::EPA::Missing Transform");
  }

  auto closestPoint = closestFaceData.closestPoint;

  glm::vec3 pointOfCollision;

  if (collider->getColliderType() == ColliderType::sphereCollider)
  {
    auto direction = glm::normalize(closestPoint);

    pointOfCollision = transform->getPosition() + direction * dynamic_cast<SphereCollider*>(collider)->getRadius();

    return pointOfCollision;
  }

  if (otherCollider->getColliderType() == ColliderType::sphereCollider)
  {
    auto direction = glm::normalize(closestPoint);

    pointOfCollision = otherTransform->getPosition() + direction * std::dynamic_pointer_cast<SphereCollider>(otherCollider)->getRadius();

    return pointOfCollision;
  }

  auto face = faces[closestFaceData.closestFaceIndex];
  auto [vertex0, direction0] = vertices[face.vertices[0]];
  auto [vertex1, direction1] = vertices[face.vertices[1]];
  auto [vertex2, direction2] = vertices[face.vertices[2]];

  auto a = otherCollider->findFurthestPoint(-direction0);
  auto b = otherCollider->findFurthestPoint(-direction1);
  auto c = otherCollider->findFurthestPoint(-direction2);

  if (a == b && b == c)
  {
    return a;
  }

  a = collider->findFurthestPoint(direction0);
  b = collider->findFurthestPoint(direction1);
  c = collider->findFurthestPoint(direction2);

  if (a == b && b == c)
  {
    return a;
  }

  auto barycentricCoordinates = computeBarycentric(vertex0, vertex1, vertex2, closestPoint);
  pointOfCollision = a * barycentricCoordinates.z + c * barycentricCoordinates.x + b * barycentricCoordinates.y;

  return pointOfCollision;
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
    if (closeEnough(currentMinDist, previousMinDist, closestFaceData.closestPoint, previousClosestPoint))
    {
      break;
    }

    auto searchDirection = getSearchDirection();

    const auto supportPoint = collider->getSupport(otherCollider, glm::normalize(searchDirection));

    if (isDuplicateVertex(supportPoint))
    {
      break;
    }

    previousMinDist = currentMinDist;
    previousClosestPoint = closestFaceData.closestPoint;

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

  vertices = {
    simplex.getSupportA(),
    simplex.getSupportB(),
    simplex.getSupportC(),
    simplex.getSupportD()
  };

  faces = {{
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

  for (int i = 0; i < faces.size(); ++i)
  {
    if (const float dist = faces[i].closestPoint.distance; dist < minDist)
    {
      minDist = dist;
      closestFaceData.closestPoint = faces[i].closestPoint.point;
      closestFaceData.closestFaceIndex = i;
    }
  }

  return minDist;
}

glm::vec3 Polytope::getSearchDirection() const
{
  glm::vec3 searchDirection = closestFaceData.closestPoint;

  if (dot(searchDirection, searchDirection) < 1e-5f)
  {
    searchDirection = faces[closestFaceData.closestFaceIndex].normal;
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

  for (int i = 0; i < faces.size();)
  {
    auto [faceVertices, normal, c] = faces[i];

    auto facePoint = vertices[faceVertices[0]].vertex;

    if (auto vectorToSupportPoint = supportPoint - facePoint; sameDirection(normal, vectorToSupportPoint))
    {
      edges.emplace_back( faceVertices[0], faceVertices[1] );
      edges.emplace_back( faceVertices[1], faceVertices[2] );
      edges.emplace_back( faceVertices[2], faceVertices[0] );

      faces.erase(faces.begin() + i);

      continue;
    }

    if (const float dist = faces[i].closestPoint.distance; dist < currentMinDist)
    {
      currentMinDist = dist;
      closestFaceData.closestPoint = faces[i].closestPoint.point;
      closestFaceData.closestFaceIndex = i;
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

bool Polytope::isFacingInward(const FaceData& faceData)
{
  for (int i = 0; i < vertices.size(); ++i)
  {
    if (i == faceData.aIndex || i == faceData.bIndex)
    {
      continue;
    }

    if (const glm::vec3 faceToVertex = vertices[i].vertex - faceData.a; sameDirection(faceData.normal, faceToVertex))
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
    .a = vertices[edge.first].vertex,
    .b = vertices[edge.second].vertex,
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
    closestFaceData.closestPoint = closestPoint;
    closestFaceData.closestFaceIndex = faces.size();
  }

  faces.push_back({
      .vertices = {
        faceData.aIndex,
        faceData.bIndex,
        static_cast<uint8_t>(vertices.size())
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

  vertices.push_back({supportPoint, direction});
}

bool Polytope::isDuplicateVertex(const glm::vec3 supportPoint)
{
  auto isEqual = [&](const SupportVertex& support) {
    return support.vertex.x == supportPoint.x &&
           support.vertex.y == supportPoint.y &&
           support.vertex.z == supportPoint.z;
  };

  return std::ranges::any_of(vertices, isEqual);
}
