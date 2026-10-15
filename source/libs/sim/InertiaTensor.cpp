#include "InertiaTensor.h"
#include <objects/Object.h>
#include <objects/components/Component.h>
#include <objects/components/RotationConvention.h>
#include <objects/components/Transform.h>
#include <objects/components/collisions/BoxCollider.h>
#include <objects/components/collisions/CapsuleCollider.h>
#include <objects/components/collisions/Collider.h>
#include <objects/components/collisions/ConvexMeshCollider.h>
#include <objects/components/collisions/SphereCollider.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/matrix.hpp>
#include <cmath>
#include <memory>

namespace {
  // Absolute, so a vanishingly small body has no shape of its own and falls back to the box of its scale.
  constexpr float minVolume = 1e-12f;

  // Tetrahedra from a point inside a convex hull to each of its triangles, summed in double precision.
  struct HullSums {
    double volume = 0.0;
    glm::dvec3 firstMoment{ 0.0 };
    glm::dmat3 secondMoment{ 0.0 };
  };

  // corners[0] is the reference point. The second moment of a tetrahedron about the origin is
  // volume / 20 * (sum of p p^T + (sum of p)(sum of p)^T).
  void addTetrahedron(HullSums& sums, const glm::dvec3 (&corners)[4])
  {
    const glm::dvec3 a = corners[1] - corners[0];
    const glm::dvec3 b = corners[2] - corners[0];
    const glm::dvec3 c = corners[3] - corners[0];
    const double volume = glm::dot(a, glm::cross(b, c)) / 6.0;
    const glm::dvec3 total = corners[0] + corners[1] + corners[2] + corners[3];

    glm::dmat3 squares(0.0);
    for (const auto& corner : corners)
    {
      squares += glm::outerProduct(corner, corner);
    }

    sums.volume += volume;
    sums.firstMoment += volume * total / 4.0;
    sums.secondMoment += volume / 20.0 * (squares + glm::outerProduct(total, total));
  }

  inertia::Distribution boxOf(BoxCollider& collider)
  {
    const auto axes = glm::mat3_cast(eulerDegreesToQuat(collider.getRotation()));

    return inertia::box(collider.getPosition(), axes, glm::abs(collider.getScale()));
  }

  inertia::Distribution sphereOf(SphereCollider& collider)
  {
    return inertia::sphere(collider.getPosition(), collider.getRadius());
  }

  inertia::Distribution capsuleOf(CapsuleCollider& collider)
  {
    const auto segment = collider.getSegment();

    return inertia::capsule(segment.start, segment.end, collider.getRadius());
  }

  inertia::Distribution hullOf(ConvexMeshCollider& collider)
  {
    return inertia::hull(collider.getWorldVertices(), collider.getHull().indices);
  }

  void collect(const Object& object, std::vector<inertia::Distribution>& shapes)
  {
    const auto collider = object.getComponent<Collider>(ComponentType::collider);
    if (collider && !collider->isTrigger() && object.getComponent<Transform>(ComponentType::transform))
    {
      if (const auto shape = inertia::of(*collider); std::isfinite(shape.volume) && shape.volume > minVolume)
      {
        shapes.push_back(shape);
      }
    }

    for (const auto& child : object.getChildren())
    {
      if (!child->getComponents().contains(ComponentType::rigidBody))
      {
        collect(*child, shapes);
      }
    }
  }

  glm::mat3 tensorOfCovariance(const glm::mat3& covariance)
  {
    const float trace = covariance[0][0] + covariance[1][1] + covariance[2][2];

    return glm::mat3(trace) - covariance;
  }
}

namespace inertia {
  Distribution sphere(const glm::vec3& center, const float radius)
  {
    const float r = std::abs(radius);

    return { 4.0f / 3.0f * glm::pi<float>() * r * r * r, center, glm::mat3(r * r / 5.0f) };
  }

  Distribution box(const glm::vec3& center, const glm::mat3& axes, const glm::vec3& halfExtents)
  {
    const glm::mat3 local(halfExtents.x * halfExtents.x / 3.0f, 0.0f, 0.0f,
                          0.0f, halfExtents.y * halfExtents.y / 3.0f, 0.0f,
                          0.0f, 0.0f, halfExtents.z * halfExtents.z / 3.0f);

    return { 8.0f * halfExtents.x * halfExtents.y * halfExtents.z, center, axes * local * glm::transpose(axes) };
  }

  Distribution capsule(const glm::vec3& start, const glm::vec3& end, const float radius)
  {
    const float r = std::abs(radius);
    const float halfLength = glm::length(end - start) * 0.5f;
    const auto center = (start + end) * 0.5f;
    const auto axis = halfLength > 0.0f ? (end - start) / (2.0f * halfLength) : glm::vec3(0.0f, 1.0f, 0.0f);

    const float cylinderVolume = glm::pi<float>() * r * r * 2.0f * halfLength;
    const float capsVolume = 4.0f / 3.0f * glm::pi<float>() * r * r * r;
    const float volume = cylinderVolume + capsVolume;
    if (volume <= minVolume)
    {
      return {};
    }

    // A hemisphere's centroid is 3r/8 from its flat face. About that centroid its second moment along the
    // axis is r^2/5 - (3r/8)^2, and r^2/5 across it.
    const float capOffset = halfLength + 0.375f * r;
    const float across = (cylinderVolume * r * r / 4.0f + capsVolume * r * r / 5.0f) / volume;
    const float along = (cylinderVolume * halfLength * halfLength / 3.0f +
                         capsVolume * (19.0f * r * r / 320.0f + capOffset * capOffset)) / volume;

    return { volume, center, glm::mat3(across) + (along - across) * glm::outerProduct(axis, axis) };
  }

  Distribution hull(const std::span<const glm::vec3> vertices, const std::span<const uint32_t> indices)
  {
    if (vertices.empty() || indices.size() < 3)
    {
      return {};
    }

    glm::dvec3 reference(0.0);
    for (const auto& vertex : vertices)
    {
      reference += glm::dvec3(vertex);
    }
    reference /= static_cast<double>(vertices.size());

    HullSums sums;
    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
      const glm::dvec3 corners[4] = { glm::dvec3(0.0), glm::dvec3(vertices[indices[i]]) - reference,
                                      glm::dvec3(vertices[indices[i + 1]]) - reference,
                                      glm::dvec3(vertices[indices[i + 2]]) - reference };
      addTetrahedron(sums, corners);
    }

    if (std::abs(sums.volume) <= minVolume)
    {
      return {};
    }

    const auto centroid = sums.firstMoment / sums.volume;
    const glm::dmat3 covariance = sums.secondMoment / sums.volume - glm::outerProduct(centroid, centroid);

    return { static_cast<float>(std::abs(sums.volume)), glm::vec3(centroid + reference), glm::mat3(covariance) };
  }

  Distribution of(Collider& collider)
  {
    switch (collider.getColliderType())
    {
      case ColliderType::boxCollider:
        return boxOf(static_cast<BoxCollider&>(collider));
      case ColliderType::sphereCollider:
        return sphereOf(static_cast<SphereCollider&>(collider));
      case ColliderType::capsuleCollider:
        return capsuleOf(static_cast<CapsuleCollider&>(collider));
      case ColliderType::convexMeshCollider:
        return hullOf(static_cast<ConvexMeshCollider&>(collider));
    }

    return {};
  }

  std::vector<Distribution> bodyDistributions(const Object& owner)
  {
    std::vector<Distribution> shapes;
    collect(owner, shapes);

    return shapes;
  }

  glm::mat3 tensorAbout(const std::span<const Distribution> shapes, const glm::vec3& point)
  {
    float totalVolume = 0.0f;
    for (const auto& shape : shapes)
    {
      totalVolume += shape.volume;
    }

    if (!(totalVolume > 0.0f) || !std::isfinite(totalVolume))
    {
      return glm::mat3(0.0f);
    }

    glm::mat3 covariance(0.0f);
    for (const auto& shape : shapes)
    {
      const auto offset = shape.centroid - point;
      covariance += shape.volume / totalVolume * (shape.covariance + glm::outerProduct(offset, offset));
    }

    return tensorOfCovariance(covariance);
  }

  glm::mat3 worldTensor(const Object* owner, const Transform& transform)
  {
    if (owner)
    {
      const auto shapes = bodyDistributions(*owner);
      if (!shapes.empty())
      {
        return tensorAbout(shapes, transform.getPosition());
      }
    }

    // A box per unit mass whose half-extents are the scale, as BoxCollider builds it.
    const auto axes = glm::mat3_cast(eulerDegreesToQuat(transform.getRotation()));
    const auto shape = box(transform.getPosition(), axes, glm::abs(transform.getScale()));

    return tensorOfCovariance(shape.covariance);
  }
}
