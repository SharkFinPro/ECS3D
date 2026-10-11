#include "NarrowPhase.h"
#include "Polytope.h"
#include "Simplex.h"
#include "Support.h"
#include <objects/components/collisions/Collider.h>
#include <objects/components/collisions/CapsuleCollider.h>
#include <objects/components/collisions/SphereCollider.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

namespace collisions {
  namespace {
    // GJK walks the Minkowski difference until the simplex encloses the origin. Fifty iterations is what
    // the transplanted implementation used; a pair that has not converged by then is reported as apart
    // rather than looped on, because the tick cannot wait for it.
    constexpr uint8_t maxIterations = 50;

    // Declared ahead of the cases they dispatch to, which sit below the entry points that use them.
    bool expandSimplex(Simplex& simplex, glm::vec3& direction);
    void lineCase(const Simplex& simplex, glm::vec3& direction);
    void triangleCase(Simplex& simplex, glm::vec3& direction);
    bool tetrahedronCase(Simplex& simplex, glm::vec3& direction);

    // A sphere is a capsule whose core segment is a single point, so one description serves the
    // sphere-sphere, sphere-capsule and capsule-capsule pairs, none of which need GJK.
    struct RoundShape {
      glm::vec3 start;
      glm::vec3 end;
      float radius;
    };

    bool isRound(const Collider& collider)
    {
      return collider.getColliderType() == ColliderType::sphereCollider ||
             collider.getColliderType() == ColliderType::capsuleCollider;
    }

    RoundShape roundShapeOf(Collider& collider)
    {
      if (collider.getColliderType() == ColliderType::capsuleCollider)
      {
        auto& capsule = dynamic_cast<CapsuleCollider&>(collider);
        const auto [start, end] = capsule.getSegment();

        return { start, end, capsule.getRadius() };
      }

      const auto center = collider.getPosition();

      return { center, center, dynamic_cast<SphereCollider&>(collider).getRadius() };
    }

    glm::vec3 closestPointOnSegment(const glm::vec3& point, const glm::vec3& start, const glm::vec3& end)
    {
      const auto segment = end - start;
      const float lengthSquared = glm::dot(segment, segment);

      if (!(lengthSquared > 0.0f))
      {
        return start;
      }

      return start + segment * std::clamp(glm::dot(point - start, segment) / lengthSquared, 0.0f, 1.0f);
    }

    // Closest points between two segments (Ericson, Real-Time Collision Detection, 5.1.9), handling
    // parallel and zero-length segments.
    std::pair<glm::vec3, glm::vec3> closestPointsBetweenSegments(const RoundShape& first, const RoundShape& second)
    {
      const auto d1 = first.end - first.start;
      const auto d2 = second.end - second.start;
      const auto r = first.start - second.start;

      const float a = glm::dot(d1, d1);
      const float e = glm::dot(d2, d2);
      const float f = glm::dot(d2, r);

      constexpr float tiny = 1e-12f;
      float s = 0.0f;
      float t = 0.0f;

      if (a <= tiny)
      {
        t = e > tiny ? std::clamp(f / e, 0.0f, 1.0f) : 0.0f;
      }
      else if (e <= tiny)
      {
        s = std::clamp(-glm::dot(d1, r) / a, 0.0f, 1.0f);
      }
      else
      {
        const float c = glm::dot(d1, r);
        const float b = glm::dot(d1, d2);
        const float denominator = a * e - b * b;

        if (denominator > 1e-6f * a * e)
        {
          s = std::clamp((b * f - c * e) / denominator, 0.0f, 1.0f);
        }

        t = (b * s + f) / e;

        if (t < 0.0f)
        {
          t = 0.0f;
          s = std::clamp(-c / a, 0.0f, 1.0f);
        }
        else if (t > 1.0f)
        {
          t = 1.0f;
          s = std::clamp((b - c) / a, 0.0f, 1.0f);
        }
      }

      return { first.start + d1 * s, second.start + d2 * t };
    }

    // Negated rather than written as "< combinedRadius", so the sweep and the response make the same
    // comparison. The two differ on a NaN distance, which a blown-up transform does produce: NaN < r is
    // false where !(NaN >= r) is true, and the pair would be apart to the sweep while still yielding a
    // contact to the response.
    bool spheresTouch(const glm::vec3& centerA, const float radiusA, const glm::vec3& centerB, const float radiusB)
    {
      return !(length(centerB - centerA) >= radiusA + radiusB);
    }

    std::optional<Contact> findSphereContact(const glm::vec3& centerA, const float radiusA,
                                             const glm::vec3& centerB, const float radiusB)
    {
      const auto combinedRadius = radiusA + radiusB;
      const auto delta = centerB - centerA;

      const float dist = length(delta);

      if (dist >= combinedRadius)
      {
        return std::nullopt;
      }

      // Concentric spheres have no separating direction to normalize, so they are pushed apart along y.
      // The result is arbitrary but has to be non-zero, or the pair stays welded together forever.
      const auto minimumTranslationVector = dist != 0.0f
        ? -(normalize(delta) * (combinedRadius - dist))
        : glm::vec3(0, combinedRadius / 2.0f, 0);

      const auto direction = -glm::normalize(minimumTranslationVector);

      // The midpoint of the overlap along the center line: halfway between where sphere A's surface
      // meets the line (moving toward B) and where sphere B's surface meets it (moving toward A). That
      // point sits inside both spheres regardless of how the radii compare, unlike either surface point
      // alone. dist is 0 for the concentric case above, where direction is the arbitrary y axis instead
      // of the (undefined) center line - the formula still produces a finite point on it.
      const auto contactOffset = (radiusA + dist - radiusB) / 2.0f;

      return Contact{ minimumTranslationVector, centerA + direction * contactOffset };
    }

    bool roundShapesOverlap(Collider& collider, Collider& other)
    {
      const auto first = roundShapeOf(collider);
      const auto second = roundShapeOf(other);
      const auto [pointA, pointB] = closestPointsBetweenSegments(first, second);

      return spheresTouch(pointA, first.radius, pointB, second.radius);
    }

    // Two segments lying side by side touch along a stretch, not at a point. This is the stretch of the
    // first segment the second covers, as distances along the first from its start.
    std::optional<std::pair<float, float>> parallelOverlap(const RoundShape& first, const RoundShape& second)
    {
      const auto d1 = first.end - first.start;
      const auto d2 = second.end - second.start;
      const float a = glm::dot(d1, d1);
      const float e = glm::dot(d2, d2);
      const auto crossed = glm::cross(d1, d2);

      constexpr float parallelSineSquared = 1e-4f;
      if (!(a > 1e-12f && e > 1e-12f) || glm::dot(crossed, crossed) > parallelSineSquared * a * e)
      {
        return std::nullopt;
      }

      const float firstLength = std::sqrt(a);
      const auto axis = d1 / firstLength;
      const float tStart = glm::dot(second.start - first.start, axis);
      const float tEnd = glm::dot(second.end - first.start, axis);

      const float low = std::max(0.0f, std::min(tStart, tEnd));
      const float high = std::min(firstLength, std::max(tStart, tEnd));

      if (!(high - low > 1e-4f))
      {
        return std::nullopt;
      }

      return std::make_pair(low, high);
    }

    // Reports the two ends of the stretch where parallel capsules overlap, so one lying on another is
    // supported at both and does not see-saw on a single point.
    void addParallelManifold(Contact& contact, const RoundShape& first, const RoundShape& second)
    {
      const auto overlap = parallelOverlap(first, second);
      if (!overlap)
      {
        return;
      }

      const auto axis = glm::normalize(first.end - first.start);

      std::array<glm::vec3, 2> points{};
      std::size_t count = 0;
      for (const float along : { overlap->first, overlap->second })
      {
        const auto pointA = first.start + axis * along;
        const auto pointB = closestPointOnSegment(pointA, second.start, second.end);

        if (const auto endContact = findSphereContact(pointA, first.radius, pointB, second.radius))
        {
          points[count++] = endContact->point;
        }
      }

      if (count == points.size())
      {
        contact.points[0] = points[0];
        contact.points[1] = points[1];
        contact.pointCount = 2;
      }
    }

    std::optional<Contact> findRoundContact(Collider& collider, Collider& other)
    {
      const auto first = roundShapeOf(collider);
      const auto second = roundShapeOf(other);
      const auto [pointA, pointB] = closestPointsBetweenSegments(first, second);

      auto contact = findSphereContact(pointA, first.radius, pointB, second.radius);
      if (contact)
      {
        addParallelManifold(*contact, first, second);
      }

      return contact;
    }

    // Leaves the terminating simplex in simplex. False means the pair does not overlap, or that GJK ran
    // out of iterations before deciding.
    bool runGjk(Collider& collider, Collider& other, Simplex& simplex)
    {
      glm::vec3 direction{ 1, 0, 0 };

      auto normalizedDirection = normalize(direction);
      auto support = getSupport(collider, other, normalizedDirection);
      simplex.addVertex({ support, normalizedDirection });

      direction *= -1.0f;

      uint8_t iteration = 0;
      do
      {
        ++iteration;

        normalizedDirection = normalize(direction);
        support = getSupport(collider, other, normalizedDirection);

        if (glm::dot(support, direction) < 0)
        {
          return false;
        }

        simplex.addVertex({ support, normalizedDirection });
      } while (iteration < maxIterations && !expandSimplex(simplex, direction));

      return iteration != maxIterations;
    }

    bool expandSimplex(Simplex& simplex, glm::vec3& direction)
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

    void lineCase(const Simplex& simplex, glm::vec3& direction)
    {
      const auto AB = simplex.getB() - simplex.getA();
      const auto AO = -simplex.getA();

      direction = cross(cross(AB, AO), AB);

      if (glm::dot(direction, direction) == 0)
      {
        direction = cross(AB, {0, 0, 1});
      }
    }

    void triangleCase(Simplex& simplex, glm::vec3& direction)
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

    bool tetrahedronCase(Simplex& simplex, glm::vec3& direction)
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
  }

  std::span<const glm::vec3> Contact::contactPoints() const
  {
    return pointCount > 1 ? std::span<const glm::vec3>(points.data(), pointCount) : std::span<const glm::vec3>(&point, 1);
  }

  float Contact::depth() const
  {
    return length(minimumTranslationVector);
  }

  glm::vec3 Contact::normal() const
  {
    return normalize(minimumTranslationVector);
  }

  std::optional<Contact> findContact(Collider& collider, Collider& other)
  {
    if (isRound(collider) && isRound(other))
    {
      return findRoundContact(collider, other);
    }

    Simplex simplex;
    if (!runGjk(collider, other, simplex))
    {
      return std::nullopt;
    }

    const Polytope polytope(collider, other, simplex);

    const auto minimumTranslationVector = polytope.getMinimumTranslationVector();

    // EPA can come back with nothing to translate along - a contact with no direction out of it is not
    // one the response can act on, so it is reported as no contact rather than as a zero-length push.
    if (minimumTranslationVector.x == 0 && minimumTranslationVector.y == 0 && minimumTranslationVector.z == 0)
    {
      return std::nullopt;
    }

    // Negated so it moves the first collider, matching the sphere path and what the response expects.
    Contact contact{ -minimumTranslationVector, polytope.findCollisionPoint() };

    const auto manifold = polytope.findContactManifold();
    if (manifold.size() > 1)
    {
      contact.pointCount = static_cast<std::uint8_t>(std::min(manifold.size(), contact.points.size()));
      std::copy_n(manifold.begin(), contact.pointCount, contact.points.begin());
    }

    return contact;
  }

  bool intersects(Collider& collider, Collider& other)
  {
    if (isRound(collider) && isRound(other))
    {
      return roundShapesOverlap(collider, other);
    }

    Simplex simplex;

    return runGjk(collider, other, simplex);
  }
}
