#include "SceneQueries.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/Transform.h>
#include <objects/components/collisions/Collider.h>
#include <objects/components/collisions/BoxCollider.h>
#include <objects/components/collisions/SphereCollider.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

namespace {
  constexpr float kEpsilon = 1e-8f;

  bool layerInMask(const std::shared_ptr<Collider>& collider, const uint32_t layerMask)
  {
    return (layerMask & (1u << collider->getLayer())) != 0u;
  }

  // The box's world matrix, built the same way BoxCollider::generateTransformedMesh does (note: the box's
  // own getScale() adds transform+local, but the collision mesh multiplies - so mirror the mesh here so
  // ray/overlap match what actually collides). Maps the unit box [-1,1]^3 into world space.
  glm::mat4 boxWorldMatrix(const std::shared_ptr<Transform>& transform, const std::shared_ptr<BoxCollider>& box)
  {
    const glm::vec3 position = transform->getPosition() + box->getLocalPosition();
    const glm::vec3 rotation = transform->getRotation() + box->getLocalRotation();
    const glm::vec3 scale = transform->getScale() * box->getLocalScale();

    return translate(glm::mat4(1.0f), position)
      * rotate(glm::mat4(1.0f), glm::radians(rotation.z), {0, 0, 1})
      * rotate(glm::mat4(1.0f), glm::radians(rotation.y), {0, 1, 0})
      * rotate(glm::mat4(1.0f), glm::radians(rotation.x), {1, 0, 0})
      * glm::scale(glm::mat4(1.0f), scale);
  }

  // Ray vs sphere. dir must be normalized. Writes the nearest forward hit distance + surface normal.
  bool raySphere(const glm::vec3& origin, const glm::vec3& dir, const glm::vec3& center, const float radius,
                 const float maxDistance, float& tHit, glm::vec3& normal)
  {
    const glm::vec3 oc = origin - center;
    const float b = dot(oc, dir);
    const float c = dot(oc, oc) - radius * radius;
    const float discriminant = b * b - c;

    if (discriminant < 0.0f)
    {
      return false;
    }

    const float sq = std::sqrt(discriminant);
    const float tNear = -b - sq;
    const float tFar = -b + sq;

    if (tFar < 0.0f)
    {
      return false; // sphere entirely behind the origin
    }

    // Origin inside the sphere (near root behind us) -> report contact at the origin, so a ray started
    // inside a collider (e.g. a body resting on/penetrating it) still detects it.
    const float t = std::max(tNear, 0.0f);
    if (t > maxDistance)
    {
      return false;
    }

    tHit = t;
    const glm::vec3 offset = (origin + dir * t) - center;
    const float offsetLength = length(offset);
    normal = offsetLength > kEpsilon ? offset / offsetLength : -dir;
    return true;
  }

  // Ray vs oriented box, via the box's world matrix: bring the ray into the box's local [-1,1]^3 space
  // (the transform is linear, so the ray parameter t is preserved and is world distance since dir is
  // normalized) and slab-test. dir must be normalized.
  bool rayBox(const glm::vec3& origin, const glm::vec3& dir, const glm::mat4& boxMatrix,
              const float maxDistance, float& tHit, glm::vec3& normal)
  {
    const glm::mat4 inverseMatrix = inverse(boxMatrix);
    const glm::vec3 localOrigin = glm::vec3(inverseMatrix * glm::vec4(origin, 1.0f));
    const glm::vec3 localDir = glm::vec3(inverseMatrix * glm::vec4(dir, 0.0f));

    float tMin = -std::numeric_limits<float>::max();
    float tMax = std::numeric_limits<float>::max();
    int hitAxis = -1;
    float hitSign = 1.0f;

    for (int axis = 0; axis < 3; ++axis)
    {
      if (std::abs(localDir[axis]) < kEpsilon)
      {
        // Ray parallel to this slab: miss if it starts outside the slab.
        if (localOrigin[axis] < -1.0f || localOrigin[axis] > 1.0f)
        {
          return false;
        }
        continue;
      }

      const float inv = 1.0f / localDir[axis];
      float t1 = (-1.0f - localOrigin[axis]) * inv; // -1 face
      float t2 = (1.0f - localOrigin[axis]) * inv;  // +1 face
      float entrySign = -1.0f;                      // entering the -1 face -> local normal -axis
      if (t1 > t2)
      {
        std::swap(t1, t2);
        entrySign = 1.0f; // entering the +1 face -> local normal +axis
      }

      if (t1 > tMin)
      {
        tMin = t1;
        hitAxis = axis;
        hitSign = entrySign;
      }
      tMax = std::min(tMax, t2);

      if (tMin > tMax)
      {
        return false;
      }
    }

    // tMax < 0 means the whole box is behind the origin. tMin < 0 <= tMax means the origin is inside the
    // box -> report contact at the origin (t = 0), so a ray started inside a collider still detects it.
    if (hitAxis < 0 || tMax < 0.0f)
    {
      return false;
    }

    const float t = std::max(tMin, 0.0f);
    if (t > maxDistance)
    {
      return false;
    }

    glm::vec3 localNormal(0.0f);
    localNormal[hitAxis] = hitSign;

    // Transform the local face normal to world with the normal matrix (inverse-transpose of the linear
    // part), so non-uniform box scale doesn't skew it.
    tHit = t;
    normal = normalize(glm::mat3(transpose(inverseMatrix)) * localNormal);
    return true;
  }

  // Whether a sphere overlaps an oriented box, via the closest point on the box to the sphere centre.
  bool sphereOverlapsBox(const glm::vec3& center, const float radius, const glm::mat4& boxMatrix)
  {
    // Box centre + the (scaled) axes are the columns of the world matrix; length of each column is the
    // half-extent along that axis, the normalized column is the axis direction.
    const glm::vec3 boxCenter = glm::vec3(boxMatrix[3]);
    const glm::vec3 delta = center - boxCenter;

    glm::vec3 closest = boxCenter;
    for (int axis = 0; axis < 3; ++axis)
    {
      const glm::vec3 column = glm::vec3(boxMatrix[axis]);
      const float extent = length(column);
      if (extent < kEpsilon)
      {
        continue;
      }

      const glm::vec3 direction = column / extent;
      const float distance = std::clamp(dot(delta, direction), -extent, extent);
      closest += direction * distance;
    }

    return dot(center - closest, center - closest) <= radius * radius;
  }
}

bool SceneQueries::raycast(ObjectManager& objectManager,
                           const glm::vec3& origin, const glm::vec3& direction, const float maxDistance,
                           const uint32_t layerMask, const uuids::uuid& ignoreObject,
                           uuids::uuid& hitObject, glm::vec3& hitPoint, glm::vec3& hitNormal, float& hitDistance)
{
  const float dirLength = length(direction);
  if (dirLength < kEpsilon)
  {
    return false;
  }
  const glm::vec3 dir = direction / dirLength;

  bool hitAnything = false;
  float nearest = maxDistance;

  for (const auto& object : objectManager.getAllObjects())
  {
    if (object->getUUID() == ignoreObject)
    {
      continue; // skip the caster's own object (nil ignoreObject matches nothing)
    }

    const auto collider = object->getComponent<Collider>(ComponentType::collider);
    if (!collider || !layerInMask(collider, layerMask))
    {
      continue;
    }

    float t = 0.0f;
    glm::vec3 normal(0.0f);
    bool hit = false;

    if (collider->getColliderType() == ColliderType::sphereCollider)
    {
      const auto sphere = std::dynamic_pointer_cast<SphereCollider>(collider);
      hit = raySphere(origin, dir, sphere->getPosition(), sphere->getRadius(), nearest, t, normal);
    }
    else
    {
      const auto box = std::dynamic_pointer_cast<BoxCollider>(collider);
      const auto transform = object->getComponent<Transform>(ComponentType::transform);
      if (box && transform)
      {
        hit = rayBox(origin, dir, boxWorldMatrix(transform, box), nearest, t, normal);
      }
    }

    if (hit && t <= nearest)
    {
      hitAnything = true;
      nearest = t;
      hitObject = object->getUUID();
      hitPoint = origin + dir * t;
      hitNormal = normal;
      hitDistance = t;
    }
  }

  return hitAnything;
}

void SceneQueries::overlapSphere(ObjectManager& objectManager,
                                 const glm::vec3& center, const float radius, const uint32_t layerMask,
                                 const uuids::uuid& ignoreObject,
                                 std::vector<uuids::uuid>& results)
{
  for (const auto& object : objectManager.getAllObjects())
  {
    if (object->getUUID() == ignoreObject)
    {
      continue; // skip the caster's own object (nil ignoreObject matches nothing)
    }

    const auto collider = object->getComponent<Collider>(ComponentType::collider);
    if (!collider || !layerInMask(collider, layerMask))
    {
      continue;
    }

    bool overlaps = false;

    if (collider->getColliderType() == ColliderType::sphereCollider)
    {
      const auto sphere = std::dynamic_pointer_cast<SphereCollider>(collider);
      const glm::vec3 delta = sphere->getPosition() - center;
      const float combined = sphere->getRadius() + radius;
      overlaps = dot(delta, delta) <= combined * combined;
    }
    else
    {
      const auto box = std::dynamic_pointer_cast<BoxCollider>(collider);
      const auto transform = object->getComponent<Transform>(ComponentType::transform);
      if (box && transform)
      {
        overlaps = sphereOverlapsBox(center, radius, boxWorldMatrix(transform, box));
      }
    }

    if (overlaps)
    {
      results.push_back(object->getUUID());
    }
  }
}
