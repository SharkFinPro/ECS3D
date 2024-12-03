#include "Collider.h"

#include "../../Object.h"
#include "../Transform.h"
#include <stdexcept>

#include <glm/glm.hpp>

bool sameDirection(glm::vec3 a, glm::vec3 b)
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

  // TODO: EPA

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
