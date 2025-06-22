#include "Collider.h"
#include "Polytope.h"
#include "SphereCollider.h"
#include "../Transform.h"
#include "../../Object.h"
#include "../../../ECS3D.h"
#include <glm/glm.hpp>
#include <stdexcept>

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

  const Polytope polytope(this, otherCollider, simplex);

  const auto minimumTranslationVector = polytope.getMinimumTranslationVector();

  if (minimumTranslationVector.y != 0 || minimumTranslationVector.x != 0 || minimumTranslationVector.z != 0)
  {
    *mtv = -minimumTranslationVector;

    const auto pointOfCollision = polytope.findCollisionPoint();

    linesToDraw.emplace_back(otherTransform->getPosition(), pointOfCollision);
    linesToDraw.emplace_back(transform_ptr.lock()->getPosition(), pointOfCollision);

    if (collisionPoint != nullptr)
    {
      *collisionPoint = pointOfCollision;
    }

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

  const glm::vec3 corners[8] = {
    {boundingBox.minX, boundingBox.minY, boundingBox.minZ},
    {boundingBox.maxX, boundingBox.minY, boundingBox.minZ},
    {boundingBox.maxX, boundingBox.maxY, boundingBox.minZ},
    {boundingBox.minX, boundingBox.maxY, boundingBox.minZ},
    {boundingBox.minX, boundingBox.minY, boundingBox.maxZ},
    {boundingBox.maxX, boundingBox.minY, boundingBox.maxZ},
    {boundingBox.maxX, boundingBox.maxY, boundingBox.maxZ},
    {boundingBox.minX, boundingBox.maxY, boundingBox.maxZ}
  };

  // Bottom face
  renderer->renderLine(corners[0], corners[1]); // min to +X
  renderer->renderLine(corners[1], corners[2]); // +X to +X+Y
  renderer->renderLine(corners[2], corners[3]); // +X+Y to +Y
  renderer->renderLine(corners[3], corners[0]); // +Y to min

  // Top face
  renderer->renderLine(corners[4], corners[5]); // +Z to +X+Z
  renderer->renderLine(corners[5], corners[6]); // +X+Z to max
  renderer->renderLine(corners[6], corners[7]); // max to +Y+Z
  renderer->renderLine(corners[7], corners[4]); // +Y+Z to +Z

  // Vertical edges connecting bottom to top
  renderer->renderLine(corners[0], corners[4]); // min to +Z
  renderer->renderLine(corners[1], corners[5]); // +X to +X+Z
  renderer->renderLine(corners[2], corners[6]); // +X+Y to max
  renderer->renderLine(corners[3], corners[7]); // +Y to +Y+Z
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

ColliderType Collider::getColliderType() const
{
  return colliderType;
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
