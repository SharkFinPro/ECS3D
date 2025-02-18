#include "Collider.h"

#include "../../Object.h"
#include "../Transform.h"
#include <stdexcept>
#include <limits>
#include <glm/glm.hpp>
#include <algorithm>
#include <map>
#include <ranges>

#include "SphereCollider.h"

bool sameDirection(const glm::vec3& first, const glm::vec3& second)
{
  return dot(first, second) > 0;
}

Collider::Collider(const ColliderType type, const ComponentType subType)
  : Component(ComponentType::collider, subType), colliderType(type), roughMaxDistance(0)
{}

bool Collider::collidesWith(const std::shared_ptr<Object>& other, glm::vec3* mtv)
{
  if (transform_ptr.expired())
  {
    transform_ptr = owner->getComponent<Transform>(ComponentType::transform);

    if (transform_ptr.expired())
    {
      throw std::runtime_error("Collider::collidesWith::Missing Transform");
    }
  }

  const auto otherTransform = other->getComponent<Transform>(ComponentType::transform);
  const auto otherCollider = other->getComponent<Collider>(ComponentType::collider);
  if (!otherTransform || !otherCollider)
  {
    return false;
  }

  if (colliderType == ColliderType::sphereCollider && otherCollider->colliderType == ColliderType::sphereCollider)
  {
    return handleSphereToSphereCollision(otherCollider, otherTransform, mtv);
  }

  Simplex simplex;
  glm::vec3 direction{1, 0, 0};

  auto support = getSupport(otherCollider, normalize(direction));
  simplex.addVertex(support);

  direction *= -1.0f;

  constexpr uint8_t maxIterations = 50;
  uint8_t iteration = 0;
  do
  {
    ++iteration;

    support = getSupport(otherCollider, normalize(direction));

    if (dot(support, direction) < 0)
    {
      return false;
    }

    simplex.addVertex(support);
  } while (iteration < maxIterations && !expandSimplex(simplex, direction));

  if (iteration == maxIterations)
  {
    return false;
  }

  if (mtv == nullptr)
  {
    return true;
  }

  Polytope polytope = generatePolytope(simplex);

  auto minimumTranslationVector = EPA(polytope, other);

  if (minimumTranslationVector.y != 0 || minimumTranslationVector.x != 0 || minimumTranslationVector.z != 0)
  {
    *mtv = -EPA(polytope, other);

    return true;
  }

  return false;
}

glm::vec3 Collider::getRoughFurthestPoint(const glm::vec3 direction)
{
  if (transform_ptr.expired())
  {
    transform_ptr = owner->getComponent<Transform>(ComponentType::transform);

    if (transform_ptr.expired())
    {
      throw std::runtime_error("Collider::collidesWith::Missing Transform");
    }
  }

  if (const std::shared_ptr<Transform> transform = transform_ptr.lock())
  {
    const auto transformScale = transform->getScale();
    const float scale = std::max({transformScale.x, transformScale.y, transformScale.z});
    return direction * 2.0f * scale + transform->getPosition();
  }

  return {0, 0, 0};
}

bool Collider::handleSphereToSphereCollision(const std::shared_ptr<Collider>& otherCollider,
                                             const std::shared_ptr<Transform>& otherTransform,
                                             glm::vec3* mtv)
{
  if (const std::shared_ptr<Transform> transform = transform_ptr.lock())
  {
    const auto sphereA = dynamic_cast<SphereCollider*>(this);
    const auto sphereB = std::dynamic_pointer_cast<SphereCollider>(otherCollider);

    const auto combinedRadius = sphereA->getRadius() + sphereB->getRadius();
    const auto delta = otherTransform->getPosition() - transform->getPosition();

    if (const float dist = length(delta); dist < combinedRadius)
    {
      if (mtv != nullptr)
      {
        *mtv = -(normalize(delta) * (combinedRadius - dist));
      }

      return true;
    }
  }

  return false;
}

glm::vec3 Collider::getSupport(const std::shared_ptr<Collider>& other, const glm::vec3& direction)
{
  return findFurthestPoint(direction) - other->findFurthestPoint(-direction);
}

bool Collider::expandSimplex(Simplex& simplex, glm::vec3& direction)
{
  switch (simplex.size())
  {
    case 2:
      lineCase(simplex, direction);
      return false;
    case 3:
      triangleCase(simplex, direction);
      return false;
    case 4:
      return tetrahedronCase(simplex, direction);
    default:
      return false;
  }
}

void Collider::lineCase(const Simplex& simplex, glm::vec3& direction)
{
  const auto AB = simplex.getB() - simplex.getA();
  const auto AO = -simplex.getA();

  direction = cross(cross(AB, AO), AB);

  if (dot(direction, direction) == 0)
  {
    direction = cross(AB, {0, 0, 1});
  }
}

void Collider::triangleCase(Simplex& simplex, glm::vec3& direction)
{
  const auto AB = simplex.getB() - simplex.getA();
  const auto AC = simplex.getC() - simplex.getA();
  const auto AO = -simplex.getA();

  const auto ABperp = cross(cross(AC, AB), AB);
  const auto ACperp = cross(cross(AB, AC), AC);

  if (sameDirection(ABperp, AO))
  {
    simplex.removeC();
    direction = ABperp;
    return;
  }

  if (sameDirection(ACperp, AO))
  {
    simplex.removeB();
    direction = ACperp;
    return;
  }

  glm::vec3 normal = cross(AB, AC);
  direction = sameDirection(normal, AO) ? normal : -normal;
}

bool Collider::tetrahedronCase(Simplex& simplex, glm::vec3& direction)
{
  const auto A = simplex.getA();
  const auto B = simplex.getB();
  const auto C = simplex.getC();
  const auto D = simplex.getD();

  const auto AB = B - A;
  const auto AC = C - A;
  const auto AD = D - A;
  const auto AO = -A;

  auto ABC = cross(AB, AC);
  auto ACD = cross(AC, AD);
  auto ADB = cross(AD, AB);

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

  if (sameDirection(ABC, AO))
  {
    simplex.removeD();
    direction = ABC;
    return false;
  }

  if (sameDirection(ACD, AO))
  {
    simplex.removeB();
    direction = ACD;
    return false;
  }

  if (sameDirection(ADB, AO))
  {
    simplex.removeC();
    direction = ADB;
    return false;
  }

  return true;
}

Polytope Collider::generatePolytope(Simplex& simplex)
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

  auto ABC = cross(AB, AC);
  auto ACD = cross(AC, AD);
  auto ADB = cross(AD, AB);
  auto BCD = cross(BC, BD);

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

  return {
    .vertices = {
      A, B, C, D
    },
    .faces = {{
      {
        .vertices = { 0, 1, 2 },
        .normal = ABC,
        .closestPoint = {
          .point = facePointA,
          .distance = dot(facePointA, facePointA)
        }
      },
      {
        .vertices = { 0, 2, 3 },
        .normal = ACD,
        .closestPoint = {
          .point = facePointB,
          .distance = dot(facePointB, facePointB)
        }
      },
      {
        .vertices = { 0, 1, 3 },
        .normal = ADB,
        .closestPoint = {
          .point = facePointC,
          .distance = dot(facePointC, facePointC)
        }
      },
      {
        .vertices = { 1, 2, 3 },
        .normal = BCD,
        .closestPoint = {
          .point = facePointD,
          .distance = dot(facePointD, facePointD)
        }
      },
    }}
  };
}

glm::vec3 Collider::closestPointOnPlane(const glm::vec3& a, const glm::vec3& normal)
{
  const auto d = dot(normal, a);

  const auto p = d / dot(normal, normal);

  return normal * p;
}

glm::vec3 Collider::EPA(Polytope& polytope, const std::shared_ptr<Object>& other)
{
  if (transform_ptr.expired())
  {
    transform_ptr = owner->getComponent<Transform>(ComponentType::transform);

    if (transform_ptr.expired())
    {
      throw std::runtime_error("Collider::EPA::Missing Transform");
    }
  }

  const auto otherTransform = other->getComponent<Transform>(ComponentType::transform);
  const auto otherCollider = other->getComponent<Collider>(ComponentType::collider);
  if (!otherTransform || !otherCollider)
  {
    throw std::runtime_error("Collider::EPA::Missing Transform/Collider");
  }

  std::optional<glm::vec3> previousClosestPoint;
  std::optional<float> previousMinDist;

  ClosestFaceData closestFaceData{};
  auto currentMinDist = findClosestFace(closestFaceData, polytope);

  constexpr uint8_t maxIterations = 25;
  uint8_t iteration = 0;
  while (iteration < maxIterations)
  {
    if (closeEnough(currentMinDist, previousMinDist, closestFaceData.closestPoint, previousClosestPoint))
    {
      break;
    }

    auto searchDirection = getSearchDirection(closestFaceData, polytope);

    const auto supportPoint = getSupport(otherCollider, normalize(searchDirection));

    if (isDuplicateVertex(supportPoint, polytope))
    {
      break;
    }

    previousMinDist = currentMinDist;
    previousClosestPoint = closestFaceData.closestPoint;

    currentMinDist = std::numeric_limits<float>::max();
    reconstructPolytope(supportPoint, polytope, currentMinDist, closestFaceData);

    ++iteration;
  }

  return closestFaceData.closestPoint;
}

float Collider::findClosestFace(ClosestFaceData& closestFaceData, const Polytope& polytope)
{
  float minDist = std::numeric_limits<float>::max();

  for (int i = 0; i < polytope.faces.size(); ++i)
  {
    if (const float dist = polytope.faces[i].closestPoint.distance; dist < minDist)
    {
      minDist = dist;
      closestFaceData.closestPoint = polytope.faces[i].closestPoint.point;
      closestFaceData.closestFaceIndex = i;
    }
  }

  return minDist;
}

bool Collider::closeEnough(const float minDistance, const std::optional<float>& previousMinDistance,
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

glm::vec3 Collider::getSearchDirection(const ClosestFaceData& closestFaceData, const Polytope& polytope)
{
  glm::vec3 searchDirection = closestFaceData.closestPoint;

  if (dot(searchDirection, searchDirection) < 1e-5f)
  {
    searchDirection = polytope.faces[closestFaceData.closestFaceIndex].normal;
  }

  return searchDirection;
}

std::vector<Edge> Collider::deconstructPolytope(glm::vec3 supportPoint, Polytope& polytope, float& currentMinDist,
                                                ClosestFaceData& closestFaceData)
{
  std::vector<Edge> edges;

  for (int i = 0; i < polytope.faces.size();)
  {
    auto [faceVertices, normal, c] = polytope.faces[i];

    auto facePoint = polytope.vertices[faceVertices[0]];

    if (auto vectorToSupportPoint = supportPoint - facePoint; sameDirection(normal, vectorToSupportPoint))
    {
      edges.emplace_back( faceVertices[0], faceVertices[1] );
      edges.emplace_back( faceVertices[1], faceVertices[2] );
      edges.emplace_back( faceVertices[2], faceVertices[0] );

      polytope.faces.erase(polytope.faces.begin() + i);

      continue;
    }

    if (const float dist = polytope.faces[i].closestPoint.distance; dist < currentMinDist)
    {
      currentMinDist = dist;
      closestFaceData.closestPoint = polytope.faces[i].closestPoint.point;
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

bool Collider::isFacingInward(const FaceData& faceData, const Polytope& polytope)
{
  for (int i = 0; i < polytope.vertices.size(); ++i)
  {
    if (i == faceData.aIndex || i == faceData.bIndex)
    {
      continue;
    }

    if (const glm::vec3 faceToVertex = polytope.vertices[i] - faceData.a; sameDirection(faceData.normal, faceToVertex))
    {
      return true;
    }
  }

  return false;
}

void Collider::constructFace(Edge edge, glm::vec3 supportPoint, Polytope& polytope, float& currentMinDist,
                             ClosestFaceData& closestFaceData)
{
  FaceData faceData {
    .aIndex = edge.first,
    .bIndex = edge.second,
    .a = polytope.vertices[edge.first],
    .b = polytope.vertices[edge.second],
    .c = supportPoint
  };

  const auto AB = faceData.b - faceData.a;
  const auto AC = faceData.c - faceData.a;

  faceData.normal = cross(AB, AC);

  if (isFacingInward(faceData, polytope))
  {
    faceData.normal *= -1;

    if (isFacingInward(faceData, polytope))
    {
      return;
    }
  }

  const auto closestPoint = closestPointOnPlane(faceData.a, faceData.normal);
  float distance = dot(closestPoint, closestPoint);

  if (distance < currentMinDist)
  {
    currentMinDist = distance;
    closestFaceData.closestPoint = closestPoint;
    closestFaceData.closestFaceIndex = polytope.faces.size();
  }

  polytope.faces.push_back({
      .vertices = {
        faceData.aIndex,
        faceData.bIndex,
        static_cast<uint8_t>(polytope.vertices.size())
      },
      .normal = faceData.normal,
      .closestPoint = {
        .point = closestPoint,
        .distance = distance
      }
  });
}

void Collider::reconstructPolytope(const glm::vec3 supportPoint, Polytope& polytope, float& currentMinDist,
                                   ClosestFaceData& closestFaceData)
{
  for (const auto& edge : deconstructPolytope(supportPoint, polytope, currentMinDist, closestFaceData))
  {
    constructFace(edge, supportPoint, polytope, currentMinDist, closestFaceData);
  }

  polytope.vertices.push_back(supportPoint);
}

bool Collider::isDuplicateVertex(const glm::vec3 supportPoint, const Polytope& polytope)
{
  auto isEqual = [&](const glm::vec3& vertex) {
    return vertex.x == supportPoint.x &&
           vertex.y == supportPoint.y &&
           vertex.z == supportPoint.z;
  };

  return std::ranges::any_of(polytope.vertices, isEqual);
}
