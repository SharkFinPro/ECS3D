#include "NarrowPhase.h"
#include "Polytope.h"
#include "Simplex.h"
#include "Support.h"
#include <objects/components/collisions/Collider.h>
#include <objects/components/collisions/SphereCollider.h>
#include <glm/glm.hpp>
#include <cstdint>

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

    // Two spheres need no GJK at all: the overlap, its depth and its point all fall out of the centers
    // and the radii. Separate from findSphereContact so the sweep, which only asks whether the pair
    // touches, does not pay for a contact point it throws away.
    bool spheresOverlap(Collider* collider, const std::shared_ptr<Collider>& other)
    {
      const auto sphereA = dynamic_cast<SphereCollider*>(collider);
      const auto sphereB = std::dynamic_pointer_cast<SphereCollider>(other);

      const auto combinedRadius = sphereA->getRadius() + sphereB->getRadius();

      return length(other->getPosition() - collider->getPosition()) < combinedRadius;
    }

    std::optional<Contact> findSphereContact(Collider* collider, const std::shared_ptr<Collider>& other)
    {
      const auto sphereA = dynamic_cast<SphereCollider*>(collider);
      const auto sphereB = std::dynamic_pointer_cast<SphereCollider>(other);

      const auto combinedRadius = sphereA->getRadius() + sphereB->getRadius();
      const auto delta = other->getPosition() - collider->getPosition();

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

      // Offset from the first sphere's center by the *second* sphere's radius, which only lands on the
      // overlap when the radii match. Kept as it was rather than corrected here: it is what the collision
      // response has always been handed, and changing it changes how bodies spin. Filed separately.
      return Contact{ minimumTranslationVector, collider->getPosition() + direction * sphereB->getRadius() };
    }

    // Leaves the terminating simplex in simplex. False means the pair does not overlap, or that GJK ran
    // out of iterations before deciding.
    bool runGjk(Collider* collider, const std::shared_ptr<Collider>& other, Simplex& simplex)
    {
      glm::vec3 direction{ 1, 0, 0 };

      auto support = getSupport(collider, other, normalize(direction));
      simplex.addVertex({ support, direction });

      direction *= -1.0f;

      uint8_t iteration = 0;
      do
      {
        ++iteration;

        support = getSupport(collider, other, normalize(direction));

        if (glm::dot(support, direction) < 0)
        {
          return false;
        }

        simplex.addVertex({ support, direction });
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

  float Contact::depth() const
  {
    return length(minimumTranslationVector);
  }

  glm::vec3 Contact::normal() const
  {
    return normalize(minimumTranslationVector);
  }

  std::optional<Contact> findContact(Collider* collider, const std::shared_ptr<Collider>& other)
  {
    if (collider->getColliderType() == ColliderType::sphereCollider &&
        other->getColliderType() == ColliderType::sphereCollider)
    {
      return findSphereContact(collider, other);
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
    return Contact{ -minimumTranslationVector, polytope.findCollisionPoint() };
  }

  bool intersects(Collider* collider, const std::shared_ptr<Collider>& other)
  {
    if (collider->getColliderType() == ColliderType::sphereCollider &&
        other->getColliderType() == ColliderType::sphereCollider)
    {
      return spheresOverlap(collider, other);
    }

    Simplex simplex;

    return runGjk(collider, other, simplex);
  }
}
