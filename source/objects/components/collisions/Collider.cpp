#include "Collider.h"
#include "SphereCollider.h"
#include "../Transform.h"
#include "../../Object.h"
#include "../../../ECS3D.h"
#include <glm/glm.hpp>
#include <stdexcept>
#include <limits>
#include <algorithm>
#include <map>
#include <ranges>

bool sameDirection(const glm::vec3& first, const glm::vec3& second)
{
  return dot(first, second) > 0;
}

Collider::Collider(const ColliderType type, const ComponentType subType)
  : Component(ComponentType::collider, subType), colliderType(type), roughMaxDistance(0)
{}

bool Collider::collidesWith(const std::shared_ptr<Object>& other, glm::vec3* mtv, glm::vec3* collisionPoint)
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
  simplex.addVertex({support, direction});

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

    simplex.addVertex({support, direction});
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

  EPA(polytope, other);

  auto minimumTranslationVector = polytope.closestFaceData.closestPoint;

  if (minimumTranslationVector.y != 0 || minimumTranslationVector.x != 0 || minimumTranslationVector.z != 0)
  {
    *mtv = -minimumTranslationVector;

    return true;
  }

  return false;
}

const BoundingBox& Collider::getBoundingBox()
{
  if (transform_ptr.expired())
  {
    transform_ptr = owner->getComponent<Transform>(ComponentType::transform);

    if (transform_ptr.expired())
    {
      throw std::runtime_error("Collider::collidesWith::Missing Transform");
    }
  }

  const std::shared_ptr<Transform> transform = transform_ptr.lock();
  const uint8_t transformUpdateID = transform->getUpdateID();

  if (boundingBox.lastUpdateID == transformUpdateID)
  {
    return boundingBox;
  }

  boundingBox.lastUpdateID = transformUpdateID;

  boundingBox.minX = findFurthestPoint({-1, 0, 0}).x;
  boundingBox.maxX = findFurthestPoint({1, 0, 0}).x;

  boundingBox.minY = findFurthestPoint({0, -1, 0}).y;
  boundingBox.maxY = findFurthestPoint({0, 1, 0}).y;

  boundingBox.minZ = findFurthestPoint({0, 0, -1}).z;
  boundingBox.maxZ = findFurthestPoint({0, 0, 1}).z;

  return boundingBox;
}

#ifdef COLLISION_BBOX_DEBUG
void Collider::fixedUpdate(float dt)
{
  getBoundingBox();
  linesToDraw.clear();
}

void Collider::variableUpdate(float dt)
{
  const auto renderer = getOwner()->getManager()->getECS()->getRenderer();

  for (const auto& line : linesToDraw)
  {
    renderer->renderLine(line.start, line.end);
  }

  // const glm::vec3 corners[8] = {
  //   {boundingBox.minX, boundingBox.minY, boundingBox.minZ},
  //   {boundingBox.maxX, boundingBox.minY, boundingBox.minZ},
  //   {boundingBox.maxX, boundingBox.maxY, boundingBox.minZ},
  //   {boundingBox.minX, boundingBox.maxY, boundingBox.minZ},
  //   {boundingBox.minX, boundingBox.minY, boundingBox.maxZ},
  //   {boundingBox.maxX, boundingBox.minY, boundingBox.maxZ},
  //   {boundingBox.maxX, boundingBox.maxY, boundingBox.maxZ},
  //   {boundingBox.minX, boundingBox.maxY, boundingBox.maxZ}
  // };
  //
  // // Bottom face
  // renderer->renderLine(corners[0], corners[1]); // min to +X
  // renderer->renderLine(corners[1], corners[2]); // +X to +X+Y
  // renderer->renderLine(corners[2], corners[3]); // +X+Y to +Y
  // renderer->renderLine(corners[3], corners[0]); // +Y to min
  //
  // // Top face
  // renderer->renderLine(corners[4], corners[5]); // +Z to +X+Z
  // renderer->renderLine(corners[5], corners[6]); // +X+Z to max
  // renderer->renderLine(corners[6], corners[7]); // max to +Y+Z
  // renderer->renderLine(corners[7], corners[4]); // +Y+Z to +Z
  //
  // // Vertical edges connecting bottom to top
  // renderer->renderLine(corners[0], corners[4]); // min to +Z
  // renderer->renderLine(corners[1], corners[5]); // +X to +X+Z
  // renderer->renderLine(corners[2], corners[6]); // +X+Y to max
  // renderer->renderLine(corners[3], corners[7]); // +Y to +Y+Z
}
#endif

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
      simplex.getSupportA(),
      simplex.getSupportB(),
      simplex.getSupportC(),
      simplex.getSupportD()
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

void Collider::EPA(Polytope& polytope, const std::shared_ptr<Object>& other)
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

  auto currentMinDist = findClosestFace(polytope);

  constexpr uint8_t maxIterations = 25;
  uint8_t iteration = 0;
  while (iteration < maxIterations)
  {
    if (closeEnough(currentMinDist, previousMinDist, polytope.closestFaceData.closestPoint, previousClosestPoint))
    {
      break;
    }

    auto searchDirection = getSearchDirection(polytope);

    const auto supportPoint = getSupport(otherCollider, normalize(searchDirection));

    if (isDuplicateVertex(supportPoint, polytope))
    {
      break;
    }

    previousMinDist = currentMinDist;
    previousClosestPoint = polytope.closestFaceData.closestPoint;

    currentMinDist = std::numeric_limits<float>::max();
    reconstructPolytope(supportPoint, searchDirection, polytope, currentMinDist);

    ++iteration;
  }

  findCollisionPoint(polytope, other);
}

float Collider::findClosestFace(Polytope& polytope)
{
  float minDist = std::numeric_limits<float>::max();

  for (int i = 0; i < polytope.faces.size(); ++i)
  {
    if (const float dist = polytope.faces[i].closestPoint.distance; dist < minDist)
    {
      minDist = dist;
      polytope.closestFaceData.closestPoint = polytope.faces[i].closestPoint.point;
      polytope.closestFaceData.closestFaceIndex = i;
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

glm::vec3 Collider::getSearchDirection(const Polytope& polytope)
{
  glm::vec3 searchDirection = polytope.closestFaceData.closestPoint;

  if (dot(searchDirection, searchDirection) < 1e-5f)
  {
    searchDirection = polytope.faces[polytope.closestFaceData.closestFaceIndex].normal;
  }

  return searchDirection;
}

std::vector<Edge> Collider::deconstructPolytope(glm::vec3 supportPoint, Polytope& polytope, float& currentMinDist)
{
  std::vector<Edge> edges;

  for (int i = 0; i < polytope.faces.size();)
  {
    auto [faceVertices, normal, c] = polytope.faces[i];

    auto facePoint = polytope.vertices[faceVertices[0]].vertex;

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
      polytope.closestFaceData.closestPoint = polytope.faces[i].closestPoint.point;
      polytope.closestFaceData.closestFaceIndex = i;
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

    if (const glm::vec3 faceToVertex = polytope.vertices[i].vertex - faceData.a; sameDirection(faceData.normal, faceToVertex))
    {
      return true;
    }
  }

  return false;
}

void Collider::constructFace(Edge edge, glm::vec3 supportPoint, Polytope& polytope, float& currentMinDist)
{
  FaceData faceData {
    .aIndex = edge.first,
    .bIndex = edge.second,
    .a = polytope.vertices[edge.first].vertex,
    .b = polytope.vertices[edge.second].vertex,
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
    polytope.closestFaceData.closestPoint = closestPoint;
    polytope.closestFaceData.closestFaceIndex = polytope.faces.size();
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

void Collider::reconstructPolytope(const glm::vec3 supportPoint, const glm::vec3 direction, Polytope& polytope,
                                   float& currentMinDist)
{
  for (const auto& edge : deconstructPolytope(supportPoint, polytope, currentMinDist))
  {
    constructFace(edge, supportPoint, polytope, currentMinDist);
  }

  polytope.vertices.push_back({supportPoint, direction});
}

bool Collider::isDuplicateVertex(const glm::vec3 supportPoint, const Polytope& polytope)
{
  auto isEqual = [&](const SupportVertex& support) {
    return support.vertex.x == supportPoint.x &&
           support.vertex.y == supportPoint.y &&
           support.vertex.z == supportPoint.z;
  };

  return std::ranges::any_of(polytope.vertices, isEqual);
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
  float d1 = dot(ab, ap);
  float d2 = dot(ac, ap);

  // Check if P is in vertex region outside A
  if (d1 <= 0.0f && d2 <= 0.0f)
  {
    return a; // Barycentric coordinates (1,0,0)
  }

  // Check if P is in vertex region outside B
  glm::vec3 bp = -b;
  float d3 = dot(ab, bp);
  float d4 = dot(ac, bp);
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
  float d5 = dot(ab, cp);
  float d6 = dot(ac, cp);
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

glm::vec3 Collider::findCollisionPoint(const Polytope& polytope, const std::shared_ptr<Object>& other)
{
  const auto otherTransform = other->getComponent<Transform>(ComponentType::transform);
  const auto otherCollider = other->getComponent<Collider>(ComponentType::collider);
  if (!otherTransform || !otherCollider)
  {
    throw std::runtime_error("Collider::EPA::Missing Transform/Collider");
  }

  auto closestPoint = polytope.closestFaceData.closestPoint;

  glm::vec3 pointOfCollision;

  if (colliderType == ColliderType::sphereCollider)
  {
    auto direction = glm::normalize(closestPoint);

    pointOfCollision = transform_ptr.lock()->getPosition() + direction * dynamic_cast<SphereCollider*>(this)->getRadius();

    linesToDraw.emplace_back(otherTransform->getPosition(), pointOfCollision);
    linesToDraw.emplace_back(transform_ptr.lock()->getPosition(), pointOfCollision);

    return pointOfCollision;
  }

  if (otherCollider->colliderType == ColliderType::sphereCollider)
  {
    auto direction = glm::normalize(closestPoint);

    pointOfCollision = otherTransform->getPosition() + direction * std::dynamic_pointer_cast<SphereCollider>(otherCollider)->getRadius();

    linesToDraw.emplace_back(otherTransform->getPosition(), pointOfCollision);
    linesToDraw.emplace_back(transform_ptr.lock()->getPosition(), pointOfCollision);

    return pointOfCollision;
  }

  auto face = polytope.faces[polytope.closestFaceData.closestFaceIndex];
  auto [vertex0, direction0] = polytope.vertices[face.vertices[0]];
  auto [vertex1, direction1] = polytope.vertices[face.vertices[1]];
  auto [vertex2, direction2] = polytope.vertices[face.vertices[2]];

  auto a = otherCollider->findFurthestPoint(-direction0);
  auto b = otherCollider->findFurthestPoint(-direction1);
  auto c = otherCollider->findFurthestPoint(-direction2);

  if (a == b && b == c)
  {
    pointOfCollision = a;

    linesToDraw.emplace_back(otherTransform->getPosition(), pointOfCollision);
    linesToDraw.emplace_back(transform_ptr.lock()->getPosition(), pointOfCollision);

    return pointOfCollision;
  }

  auto barycentricCoordinates = computeBarycentric(vertex0, vertex1, vertex2, closestPoint);

  pointOfCollision = a * barycentricCoordinates.z + c * barycentricCoordinates.x + b * barycentricCoordinates.y;

  if (glm::distance(otherTransform->getPosition(), pointOfCollision) > glm::distance(transform_ptr.lock()->getPosition(), otherTransform->getPosition()))
  {
    a = findFurthestPoint(direction0);
    b = findFurthestPoint(direction1);
    c = findFurthestPoint(direction2);

    pointOfCollision = a * barycentricCoordinates.z + c * barycentricCoordinates.x + b * barycentricCoordinates.y;
  }

  // TODO: Better handle the case where the closestFaceData.closestPoint is not within the closest face itself
  if (glm::distance(otherTransform->getPosition(), pointOfCollision) > glm::distance(transform_ptr.lock()->getPosition(), otherTransform->getPosition()))
  {
    closestPoint = closestPointOnTriangleToOrigin(vertex0, vertex1, vertex2);
    barycentricCoordinates = computeBarycentric(vertex0, vertex1, vertex2, closestPoint);

    pointOfCollision = a * barycentricCoordinates.z + c * barycentricCoordinates.x + b * barycentricCoordinates.y;

    if (glm::distance(otherTransform->getPosition(), pointOfCollision) > glm::distance(transform_ptr.lock()->getPosition(), otherTransform->getPosition()))
    {
      a = otherCollider->findFurthestPoint(-direction0);
      b = otherCollider->findFurthestPoint(-direction1);
      c = otherCollider->findFurthestPoint(-direction2);

      pointOfCollision = a * barycentricCoordinates.z + c * barycentricCoordinates.x + b * barycentricCoordinates.y;
    }
  }

  linesToDraw.emplace_back(otherTransform->getPosition(), pointOfCollision);
  linesToDraw.emplace_back(transform_ptr.lock()->getPosition(), pointOfCollision);

  return pointOfCollision;
}
