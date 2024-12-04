#include "Collider.h"

#include "../../Object.h"
#include "../Transform.h"
#include <stdexcept>
#include <limits>
#include <glm/glm.hpp>

bool sameDirection(const glm::vec3& a, const glm::vec3& b)
{
  return dot(a, b) > 0;
}

Collider::Collider()
  : Component(ComponentType::collider)
{}

bool Collider::collidesWith(const std::shared_ptr<Object>& other, glm::vec3* mtv)
{
  if (transform_ptr.expired())
  {
    transform_ptr = std::dynamic_pointer_cast<Transform>(owner->getComponent(ComponentType::transform));

    if (transform_ptr.expired())
    {
      throw std::runtime_error("Collider::collidesWith::Missing Transform");
    }
  }

  const auto otherTransform = std::dynamic_pointer_cast<Transform>(other->getComponent(ComponentType::transform));
  const auto otherCollider = std::dynamic_pointer_cast<Collider>(other->getComponent(ComponentType::collider));
  if (!otherTransform || !otherCollider)
  {
    return false;
  }

  Simplex simplex;
  glm::vec3 direction{1, 0, 0};

  glm::vec3 support = getSupport(otherCollider, glm::normalize(direction));
  simplex.addVertex(support);

  direction *= -1.0f;

  do
  {
    support = getSupport(otherCollider, glm::normalize(direction));

    if (dot(support, direction) < 0)
    {
      return false;
    }

    simplex.addVertex(support);
  } while (!expandSimplex(simplex, direction));

  if (mtv != nullptr)
  {
    Polytope polytope = generatePolytope(simplex);

    *mtv = EPA(polytope, other);
  }

  return true;
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
      return lineCase(simplex, direction);
    case 3:
      return triangleCase(simplex, direction);
    case 4:
      return tetrahedronCase(simplex, direction);
    default:
      return false;
  }
}

bool Collider::lineCase(const Simplex& simplex, glm::vec3& direction)
{
  const auto AB = simplex.getB() - simplex.getA();
  const auto AO = -simplex.getA();

  direction = cross(cross(AB, AO), AB);

  if (dot(direction, direction) == 0)
  {
    direction = cross(AB, {0, 0, 1});
  }

  return false;
}

bool Collider::triangleCase(Simplex& simplex, glm::vec3& direction)
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
    return false;
  }

  if (sameDirection(ACperp, AO))
  {
    simplex.removeB();
    direction = ACperp;
    return false;
  }

  glm::vec3 normal = cross(AB, AC);
  direction = sameDirection(normal, AO) ? normal : -normal;

  return false;
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
  return { 0, 0, 0 };
}

float Collider::findClosestFace(ClosestFaceData& closestFaceData, const Polytope& polytope)
{
  float minDist = std::numeric_limits<float>::max();

  for (int i = 0; i < polytope.faces.size(); i++)
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
