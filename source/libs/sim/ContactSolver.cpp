#include "ContactSolver.h"
#include "ComponentAccess.h"
#include "PhysicsSystem.h"
#include <objects/Object.h>
#include <objects/components/Component.h>
#include <objects/components/RigidBody.h>
#include <objects/components/Transform.h>
#include <objects/components/collisions/CapsuleCollider.h>
#include <objects/components/collisions/Collider.h>
#include <objects/components/collisions/SphereCollider.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <functional>

namespace {
  // Each pass hands load one contact further through a stack, and the impulses carried over from the last tick
  // do the rest. Thirty or so is what a stack ten bodies high needs to stand without leaning.
  constexpr int velocityPasses = 32;
  constexpr int separationPasses = 4;

  // A resting pair is left this far into each other, so its contact is an overlap the narrow phase measures
  // rather than a touch it has to be pressed together to find.
  constexpr float penetrationSlop = 0.0005f;

  // The share of the overlap past the slop taken out in one tick. Short of all of it, so the contacts of a pile
  // do not overshoot one another.
  constexpr float separationShare = 0.8f;

  // Units per second. Two bodies closing slower than this - one settling, or pressed on by gravity each tick - do
  // not bounce, or a resting pile would keep hopping.
  constexpr float minBounceSpeed = 2.0f;

  // Cosine of 45 degrees. A contact presses a resting body into its support, as a body stacked on it does, only
  // this close to the support's normal.
  constexpr float stackedNormalCosine = 0.7071f;

  // A contact whose normal leans this little from level is a side contact, which holds no body up.
  constexpr float sideContactNormalY = 0.0175f;

  // Rolling resistance as a share of the friction coefficient: a ball on a rough floor stops within a few of its
  // own widths rather than rolling on until the angular damping wears it down.
  constexpr float rollingResistanceShare = 0.1f;

  // Carried-over impulses belong to a point only while it stays about where it was.
  constexpr float cachedPointReach = 0.2f;
  constexpr float cachedNormalCosine = 0.95f;

  constexpr float minMove = 1e-7f;

  struct SolverBody {
    RigidBody* body = nullptr;
    Transform* transform = nullptr;
    glm::vec3 center{ 0 };
    float inverseMass = 0.0f;
    glm::mat3 inverseInertia{ 0.0f };
    // Units and radians per tick.
    glm::vec3 velocity{ 0 };
    glm::vec3 spin{ 0 };
    // The move out of overlap, kept apart from the velocity so it adds no speed.
    glm::vec3 push{ 0 };
    glm::vec3 turn{ 0 };
    bool resting = false;
    glm::vec3 supportNormal{ 0.0f, 1.0f, 0.0f };
    // Held steady on a support this tick, and the spin of what holds it.
    bool supported = false;
    glm::vec3 supportSpin{ 0 };
  };

  struct SolverPoint {
    glm::vec3 position{ 0 };
    glm::vec3 arm{ 0 };
    glm::vec3 otherArm{ 0 };
    float normalMass = 0.0f;
    std::array<float, 2> tangentMass{};
    float normalImpulse = 0.0f;
    std::array<float, 2> tangentImpulse{};
    float pushImpulse = 0.0f;
    // Negative for a point still short of the other shape by that much.
    float depth = 0.0f;
    // The least speed the point may separate at: negative for a point short of its resting overlap, which may
    // close the distance it has left this tick and no more.
    float separatingSpeed = 0.0f;
  };

  struct Constraint {
    size_t body = 0;
    size_t other = 0;
    const ContactPair* pair = nullptr;
    glm::vec3 normal{ 0 };
    std::array<glm::vec3, 2> tangents{};
    // How much of each impulse the two sides take themselves. Less than all of it for a resting body under a
    // heavier one, whose support takes the rest.
    float share = 1.0f;
    float otherShare = 1.0f;
    float friction = 0.0f;
    // How much of the speed its bodies close at they part with again.
    float restitution = 0.0f;
    std::array<SolverPoint, 4> points{};
    size_t pointCount = 0;
    // Angular impulse per unit of load that resists rolling, zero when neither side rolls.
    float rollingGrip = 0.0f;
    // The only axis a capsule lying along its contact rolls about, or zero when it rolls about any.
    glm::vec3 rollAxis{ 0 };
    glm::vec3 rollingImpulse{ 0 };
  };

  [[nodiscard]] glm::vec3 velocityAt(const SolverBody& body, const glm::vec3& arm)
  {
    return body.velocity + glm::cross(body.spin, arm);
  }

  [[nodiscard]] glm::vec3 pushAt(const SolverBody& body, const glm::vec3& arm)
  {
    return body.push + glm::cross(body.turn, arm);
  }

  void applyImpulse(SolverBody& body, const glm::vec3& impulse, const glm::vec3& arm)
  {
    body.velocity += impulse * body.inverseMass;
    body.spin += body.inverseInertia * glm::cross(arm, impulse);
  }

  void applyPush(SolverBody& body, const glm::vec3& impulse, const glm::vec3& arm)
  {
    body.push += impulse * body.inverseMass;
    body.turn += body.inverseInertia * glm::cross(arm, impulse);
  }

  // How fast a unit impulse along direction at arm moves that point along it.
  [[nodiscard]] float responseAlong(const SolverBody& body, const glm::vec3& arm, const glm::vec3& direction)
  {
    return body.inverseMass + glm::dot(glm::cross(body.inverseInertia * glm::cross(arm, direction), arm), direction);
  }

  [[nodiscard]] float inverseOrZero(const float value)
  {
    return value > 1e-12f ? 1.0f / value : 0.0f;
  }

  // A resting body pressed into its support by a heavier one moves as if it weighed what presses on it: the
  // support takes the rest of the push. Each contact of a stack then joins two bodies of like mass, however
  // unlike the masses in it, and the stack settles as one of equal bodies does instead of the heavy body
  // sinking through the light one for as long as their impulses take to even out.
  [[nodiscard]] float shareTakenBy(const SolverBody& side, const SolverBody& pusher, const glm::vec3& pushDirection)
  {
    if (!side.body || !pusher.body || !side.resting ||
        glm::dot(pushDirection, side.supportNormal) >= -stackedNormalCosine)
    {
      return 1.0f;
    }

    return std::min(1.0f, pusher.inverseMass / side.inverseMass);
  }

  // How much of a hit on the side bounces: all of it for a body in flight or one struck level with its support,
  // as one ball striking another on a table, and less the more the hit presses the body into what holds it up,
  // down to none from 45 degrees on. What lands on a resting body lands on its support too, and like anything
  // landing on static geometry it does not bounce.
  [[nodiscard]] float bounceOf(const SolverBody& side, const glm::vec3& pushDirection)
  {
    if (!side.resting)
    {
      return 1.0f;
    }

    const float intoSupport = -glm::dot(pushDirection, side.supportNormal);

    return std::clamp(1.0f - intoSupport / stackedNormalCosine, 0.0f, 1.0f);
  }

  // Free bodies trade their closing speed.
  [[nodiscard]] float restitutionOf(const SolverBody& body, const SolverBody& other, const glm::vec3& normal)
  {
    if (!body.body || !other.body)
    {
      return 0.0f;
    }

    return std::min(bounceOf(body, normal), bounceOf(other, -normal));
  }

  // The geometric mean of two bodies' coefficients. Static geometry has none of its own, so the body's applies.
  [[nodiscard]] float frictionOf(const SolverBody& body, const SolverBody& other)
  {
    const float own = body.body ? std::max(body.body->getFriction(), 0.0f) : -1.0f;
    const float others = other.body ? std::max(other.body->getFriction(), 0.0f) : -1.0f;
    if (own < 0.0f)
    {
      return std::max(others, 0.0f);
    }

    return others < 0.0f ? own : std::sqrt(own * others);
  }

  struct Rolling {
    float radius = 0.0f;
    glm::vec3 axis{ 0 };
  };

  // The world radius a sphere or capsule rolls with, and a capsule's axis. No radius for any other shape.
  [[nodiscard]] Rolling rollingOf(Collider& collider)
  {
    if (collider.getColliderType() == ColliderType::sphereCollider)
    {
      return { static_cast<SphereCollider&>(collider).getRadius(), glm::vec3(0) };
    }

    if (collider.getColliderType() != ColliderType::capsuleCollider)
    {
      return {};
    }

    auto& capsule = static_cast<CapsuleCollider&>(collider);
    const auto core = capsule.getSegment();
    const auto along = core.end - core.start;
    const float length = glm::length(along);

    return { capsule.getRadius(), length > 1e-6f ? along / length : glm::vec3(0) };
  }

  void setUpRolling(Constraint& constraint, const ContactPair& pair)
  {
    auto rolling = rollingOf(*pair.collider);
    if (rolling.radius <= 0.0f)
    {
      rolling = rollingOf(*pair.other);
    }

    if (rolling.radius <= 0.0f)
    {
      return;
    }

    constraint.rollingGrip = rollingResistanceShare * constraint.friction * rolling.radius;

    // Lying along a line the capsule's contacts already hold it against pitching, so only its own axis is free.
    if (constraint.pointCount == 2)
    {
      const auto inPlane = rolling.axis - glm::dot(rolling.axis, constraint.normal) * constraint.normal;
      const float length = glm::length(inPlane);
      constraint.rollAxis = length > 1e-6f ? inPlane / length : glm::vec3(0);
    }
  }

  class Island {
  public:
    Island(const std::span<const ContactPair> pairs, const ContactSolver::Cache& previous, const float dt)
      : m_dt(dt)
    {
      // Slot zero stands for all static geometry: it has no inverse mass, so no impulse moves it.
      m_bodies.emplace_back();

      m_constraints.reserve(pairs.size());
      for (const auto& pair : pairs)
      {
        addConstraint(pair, previous);
      }
    }

    void solve()
    {
      bounce();

      for (auto& constraint : m_constraints)
      {
        warmStart(constraint);
      }

      // Alternating the direction carries a change both up and down a stack within two passes.
      for (int pass = 0; pass < velocityPasses; ++pass)
      {
        if (pass % 2 == 0)
        {
          std::ranges::for_each(m_constraints, [this](Constraint& constraint) { relax(constraint); });
        }
        else
        {
          std::ranges::for_each(m_constraints.rbegin(), m_constraints.rend(),
                                [this](Constraint& constraint) { relax(constraint); });
        }
      }

      settleOnSupports(false);

      for (int pass = 0; pass < separationPasses; ++pass)
      {
        for (auto& constraint : m_constraints)
        {
          separate(constraint);
        }
      }

      settleOnSupports(true);
    }

    void finish(std::vector<ContactSolver::CachedContact>& resolved)
    {
      for (const auto& constraint : m_constraints)
      {
        noteSupport(constraint);
        resolved.push_back(cached(constraint));
      }

      for (size_t i = 1; i < m_bodies.size(); ++i)
      {
        write(m_bodies[i]);
      }
    }

  private:
    float m_dt;
    std::vector<SolverBody> m_bodies;
    std::unordered_map<const RigidBody*, size_t> m_bodyIndex;
    std::vector<Constraint> m_constraints;

    [[nodiscard]] size_t indexOf(RigidBody* body)
    {
      if (!body)
      {
        return 0;
      }

      const auto [entry, inserted] = m_bodyIndex.try_emplace(body, m_bodies.size());
      if (!inserted)
      {
        return entry->second;
      }

      const auto transform = componentAccess::own<Transform>(*body->getOwner(), ComponentType::transform);
      const float mass = body->getMass();
      if (!transform || !(mass > 0.0f))
      {
        entry->second = 0;
        return 0;
      }

      SolverBody solverBody;
      solverBody.body = body;
      solverBody.transform = transform;
      solverBody.center = transform->getPosition();
      solverBody.inverseMass = 1.0f / mass;
      if (const auto inverseInertia = PhysicsSystem::worldInverseInertia(*body, *transform))
      {
        solverBody.inverseInertia = *inverseInertia * solverBody.inverseMass;
      }
      solverBody.velocity = body->getVelocity();
      solverBody.spin = glm::radians(body->getAngularVelocity()) * m_dt;
      solverBody.resting = !body->isFalling();
      const auto supportNormal = body->getSupportNormal();
      if (glm::dot(supportNormal, supportNormal) > 0.0f)
      {
        solverBody.supportNormal = supportNormal;
      }

      m_bodies.push_back(solverBody);

      return entry->second;
    }

    void addConstraint(const ContactPair& pair, const ContactSolver::Cache& previous)
    {
      // A contact with no direction has nothing to resolve, and its normal would carry NaN into every body of the
      // island.
      const float mtvLength = glm::length(pair.contact.minimumTranslationVector);
      if (!(mtvLength > 0.0f) || !std::isfinite(mtvLength))
      {
        return;
      }

      Constraint constraint;
      constraint.pair = &pair;
      constraint.body = indexOf(pair.body);
      constraint.other = indexOf(pair.otherBody);
      if (constraint.body == constraint.other)
      {
        return;
      }

      const auto& body = m_bodies[constraint.body];
      const auto& other = m_bodies[constraint.other];
      const auto normal = pair.contact.normal();

      constraint.normal = normal;
      constraint.tangents[0] = glm::normalize(std::fabs(normal.x) > 0.57735f ? glm::vec3(normal.y, -normal.x, 0.0f)
                                                                              : glm::vec3(0.0f, normal.z, -normal.y));
      constraint.tangents[1] = glm::cross(normal, constraint.tangents[0]);
      constraint.friction = frictionOf(body, other);
      constraint.share = shareTakenBy(body, other, normal);
      constraint.otherShare = shareTakenBy(other, body, -normal);

      constraint.restitution = restitutionOf(body, other, normal);
      const auto contactPoints = pair.contact.contactPoints();
      constraint.pointCount = std::min(contactPoints.size(), constraint.points.size());

      for (size_t i = 0; i < constraint.pointCount; ++i)
      {
        auto& point = constraint.points[i];
        point.position = contactPoints[i];
        point.depth = pair.contact.depthAt(i);
        point.arm = point.position - body.center;
        point.otherArm = point.position - other.center;
        point.normalMass = inverseOrZero(response(constraint, point, normal));
        for (size_t t = 0; t < 2; ++t)
        {
          point.tangentMass[t] = inverseOrZero(response(constraint, point, constraint.tangents[t]));
        }

        point.separatingSpeed = std::min(point.depth - penetrationSlop, 0.0f);
      }

      setUpRolling(constraint, pair);
      loadCached(constraint, previous);
      m_constraints.push_back(constraint);
    }

    static void loadCached(Constraint& constraint, const ContactSolver::Cache& previous)
    {
      const auto key = ContactSolver::keyOf(constraint.pair->collider, constraint.pair->other);
      const auto found = previous.find(key);
      if (found == previous.end())
      {
        return;
      }

      // The cached impulses are on the cached pair's first collider, which may be this pair's second.
      const auto& cached = found->second;
      const float side = cached.first == constraint.pair->collider ? 1.0f : -1.0f;
      if (glm::dot(cached.normal * side, constraint.normal) < cachedNormalCosine)
      {
        return;
      }

      constraint.rollingImpulse = cached.rollingImpulse * side;

      std::array<bool, 4> taken{};
      for (size_t i = 0; i < constraint.pointCount; ++i)
      {
        auto& point = constraint.points[i];
        size_t nearest = cached.pointCount;
        float nearestDistance = cachedPointReach * cachedPointReach;
        for (size_t c = 0; c < cached.pointCount; ++c)
        {
          const auto offset = cached.points[c].position - point.position;
          const float distance = glm::dot(offset, offset);
          const bool onlyPoint = constraint.pointCount == 1 && cached.pointCount == 1;
          if (!taken[c] && (distance < nearestDistance || onlyPoint))
          {
            nearest = c;
            nearestDistance = distance;
          }
        }

        if (nearest == cached.pointCount)
        {
          continue;
        }

        taken[nearest] = true;
        point.normalImpulse = cached.points[nearest].normalImpulse;
        for (size_t t = 0; t < 2; ++t)
        {
          point.tangentImpulse[t] = glm::dot(cached.points[nearest].frictionImpulse * side, constraint.tangents[t]);
        }
      }
    }

    // Bodies that bounce trade their closing speed before anything else is solved, one pair at a time and over
    // again while any pair is still closing fast enough: a ball striking a row of touching ones hands its speed
    // from each to the next and sends the last one off, where solving the row's contacts together would share
    // the blow out among all of them.
    void bounce()
    {
      // A row hands a hit on by one pair a sweep when its contacts happen to be in the wrong order.
      constexpr int maxSweeps = 16;

      bool bounced = true;
      for (int sweep = 0; sweep < maxSweeps && bounced; ++sweep)
      {
        bounced = false;
        for (auto& constraint : m_constraints)
        {
          if (constraint.restitution <= 0.0f)
          {
            continue;
          }

          auto& body = m_bodies[constraint.body];
          auto& other = m_bodies[constraint.other];
          for (size_t i = 0; i < constraint.pointCount; ++i)
          {
            const auto& point = constraint.points[i];
            const float closing = -glm::dot(velocityAt(body, point.arm) - velocityAt(other, point.otherArm),
                                            constraint.normal);

            // Only a point that is touching, or reaches the other shape this tick, has anything to bounce off.
            if (closing < minBounceSpeed * m_dt || closing <= -point.depth)
            {
              continue;
            }

            apply(constraint, point,
                  (1.0f + constraint.restitution) * closing * point.normalMass * constraint.normal);
            bounced = true;
          }
        }
      }
    }

    void warmStart(Constraint& constraint)
    {
      auto& body = m_bodies[constraint.body];
      auto& other = m_bodies[constraint.other];

      for (size_t i = 0; i < constraint.pointCount; ++i)
      {
        const auto& point = constraint.points[i];
        const auto impulse = point.normalImpulse * constraint.normal +
                             point.tangentImpulse[0] * constraint.tangents[0] +
                             point.tangentImpulse[1] * constraint.tangents[1];
        apply(constraint, point, impulse);
      }

      body.spin += body.inverseInertia * constraint.rollingImpulse * constraint.share;
      other.spin -= other.inverseInertia * constraint.rollingImpulse * constraint.otherShare;
    }

    // How fast a unit impulse along direction closes or opens the point, each side taking its share.
    [[nodiscard]] float response(const Constraint& constraint, const SolverPoint& point,
                                 const glm::vec3& direction) const
    {
      return constraint.share * responseAlong(m_bodies[constraint.body], point.arm, direction) +
             constraint.otherShare * responseAlong(m_bodies[constraint.other], point.otherArm, direction);
    }

    // Pushes the first side by the impulse and the second back by it, each by its share.
    void apply(const Constraint& constraint, const SolverPoint& point, const glm::vec3& impulse)
    {
      applyImpulse(m_bodies[constraint.body], impulse * constraint.share, point.arm);
      applyImpulse(m_bodies[constraint.other], -impulse * constraint.otherShare, point.otherArm);
    }

    void relax(Constraint& constraint)
    {
      auto& body = m_bodies[constraint.body];
      auto& other = m_bodies[constraint.other];

      resistRolling(constraint);

      // Friction first: the normal impulses then have the last word, so no pass ends with a point sinking.
      holdSliding(constraint);

      for (size_t i = 0; i < constraint.pointCount; ++i)
      {
        auto& point = constraint.points[i];
        const float separating = glm::dot(velocityAt(body, point.arm) - velocityAt(other, point.otherArm),
                                          constraint.normal);

        // A contact only pushes, so it is the total that stays positive: a later pass may take back what an
        // earlier one gave.
        const float wanted = std::max(point.normalImpulse + point.normalMass * (point.separatingSpeed - separating), 0.0f);
        const auto impulse = (wanted - point.normalImpulse) * constraint.normal;
        point.normalImpulse = wanted;
        apply(constraint, point, impulse);
      }
    }

    void holdSliding(Constraint& constraint)
    {
      auto& body = m_bodies[constraint.body];
      auto& other = m_bodies[constraint.other];

      for (size_t i = 0; i < constraint.pointCount; ++i)
      {
        auto& point = constraint.points[i];
        const auto relative = velocityAt(body, point.arm) - velocityAt(other, point.otherArm);

        auto wanted = point.tangentImpulse;
        for (size_t t = 0; t < 2; ++t)
        {
          wanted[t] -= point.tangentMass[t] * glm::dot(relative, constraint.tangents[t]);
        }

        // Coulomb: the friction at a point holds up to the coefficient times the impulse pressing it together.
        const float grip = constraint.friction * point.normalImpulse;
        const float length = std::sqrt(wanted[0] * wanted[0] + wanted[1] * wanted[1]);
        if (length > grip)
        {
          const float scale = length > 0.0f ? grip / length : 0.0f;
          wanted[0] *= scale;
          wanted[1] *= scale;
        }

        const auto impulse = (wanted[0] - point.tangentImpulse[0]) * constraint.tangents[0] +
                             (wanted[1] - point.tangentImpulse[1]) * constraint.tangents[1];
        point.tangentImpulse = wanted;
        apply(constraint, point, impulse);
      }
    }

    void resistRolling(Constraint& constraint)
    {
      if (constraint.rollingGrip <= 0.0f)
      {
        return;
      }

      auto& body = m_bodies[constraint.body];
      auto& other = m_bodies[constraint.other];

      float load = 0.0f;
      for (size_t i = 0; i < constraint.pointCount; ++i)
      {
        load += constraint.points[i].normalImpulse;
      }

      const auto relative = body.spin - other.spin;
      auto rolling = relative - glm::dot(relative, constraint.normal) * constraint.normal;
      if (glm::dot(constraint.rollAxis, constraint.rollAxis) > 0.0f)
      {
        rolling = glm::dot(rolling, constraint.rollAxis) * constraint.rollAxis;
      }

      const float rate = glm::length(rolling);
      auto wanted = constraint.rollingImpulse;
      if (rate > 1e-9f)
      {
        const auto axis = rolling / rate;
        const float turning = glm::dot(axis, (body.inverseInertia * constraint.share +
                                              other.inverseInertia * constraint.otherShare) * axis);
        wanted -= inverseOrZero(turning) * rolling;
      }

      const float limit = constraint.rollingGrip * load;
      const float length = glm::length(wanted);
      if (length > limit)
      {
        wanted *= length > 0.0f ? limit / length : 0.0f;
      }

      const auto impulse = wanted - constraint.rollingImpulse;
      constraint.rollingImpulse = wanted;
      body.spin += body.inverseInertia * impulse * constraint.share;
      other.spin -= other.inverseInertia * impulse * constraint.otherShare;
    }

    void separate(Constraint& constraint)
    {
      auto& body = m_bodies[constraint.body];
      auto& other = m_bodies[constraint.other];

      for (size_t i = 0; i < constraint.pointCount; ++i)
      {
        auto& point = constraint.points[i];
        const float target = separationShare * std::max(point.depth - penetrationSlop, 0.0f);
        const float moved = glm::dot(pushAt(body, point.arm) - pushAt(other, point.otherArm), constraint.normal);
        const float wanted = std::max(point.pushImpulse + point.normalMass * (target - moved), 0.0f);
        const auto impulse = (wanted - point.pushImpulse) * constraint.normal;
        point.pushImpulse = wanted;
        applyPush(body, impulse * constraint.share, point.arm);
        applyPush(other, -impulse * constraint.otherShare, point.otherArm);
      }
    }

    // The passes above share a load between the two bodies of each contact, so they can end with a body still
    // sinking a little into the one below. This last pass walks up from the ground and stops each body on the one
    // holding it up as it would on static geometry. The impulse joins the contact's total, so the next tick
    // starts with the body below carrying it too.
    void settleOnSupports(const bool pushes)
    {
      std::vector<bool> grounded(m_bodies.size(), false);
      grounded[0] = true;

      for (auto& constraint : m_constraints)
      {
        const bool bodyAbove = constraint.normal.y > stackedNormalCosine;
        if (!bodyAbove && constraint.normal.y > -stackedNormalCosine)
        {
          continue;
        }

        const size_t above = bodyAbove ? constraint.body : constraint.other;
        const size_t below = bodyAbove ? constraint.other : constraint.body;
        if (!grounded[below] || above == 0)
        {
          continue;
        }

        if (settleOn(constraint, bodyAbove, pushes) > 0.0f)
        {
          grounded[above] = true;
        }
      }
    }

    // Stops the upper body of the contact against the lower one, which is not moved. Returns the contact's load.
    float settleOn(Constraint& constraint, const bool bodyAbove, const bool pushes)
    {
      auto& body = m_bodies[constraint.body];
      auto& other = m_bodies[constraint.other];
      auto& upper = bodyAbove ? body : other;
      const float side = bodyAbove ? 1.0f : -1.0f;
      float load = 0.0f;

      // Several times over the points, since the impulse at each one disturbs the others.
      for (int pass = 0; pass < 8; ++pass)
      {
        // Its sliding is held the same way, or a stack on a slope would creep for as long as the passes above
        // take to share its friction out. Before the normal impulses, so they have the last word here too.
        for (size_t i = 0; !pushes && i < constraint.pointCount; ++i)
        {
          auto& point = constraint.points[i];
          const auto& arm = bodyAbove ? point.arm : point.otherArm;
          const auto relative = velocityAt(body, point.arm) - velocityAt(other, point.otherArm);

          auto wanted = point.tangentImpulse;
          for (size_t t = 0; t < 2; ++t)
          {
            wanted[t] -= inverseOrZero(responseAlong(upper, arm, constraint.tangents[t])) *
                         glm::dot(relative, constraint.tangents[t]);
          }

          const float grip = constraint.friction * point.normalImpulse;
          const float length = std::sqrt(wanted[0] * wanted[0] + wanted[1] * wanted[1]);
          if (length > grip)
          {
            const float scale = length > 0.0f ? grip / length : 0.0f;
            wanted[0] *= scale;
            wanted[1] *= scale;
          }

          for (size_t t = 0; t < 2; ++t)
          {
            applyImpulse(upper, side * (wanted[t] - point.tangentImpulse[t]) * constraint.tangents[t], arm);
            point.tangentImpulse[t] = wanted[t];
          }
        }

        load = 0.0f;
        for (size_t i = 0; i < constraint.pointCount; ++i)
        {
          auto& point = constraint.points[i];
          const auto& arm = bodyAbove ? point.arm : point.otherArm;
          const float mass = inverseOrZero(responseAlong(upper, arm, constraint.normal));

          if (pushes)
          {
            const float target = separationShare * std::max(point.depth - penetrationSlop, 0.0f);
            const float moved = glm::dot(pushAt(body, point.arm) - pushAt(other, point.otherArm),
                                         constraint.normal);
            const float wanted = std::max(point.pushImpulse + mass * (target - moved), 0.0f);
            applyPush(upper, side * (wanted - point.pushImpulse) * constraint.normal, arm);
            point.pushImpulse = wanted;
            load += point.normalImpulse + wanted;
            continue;
          }

          const float separating = glm::dot(velocityAt(body, point.arm) - velocityAt(other, point.otherArm),
                                            constraint.normal);
          const float wanted = std::max(point.normalImpulse + mass * (point.separatingSpeed - separating), 0.0f);
          applyImpulse(upper, side * (wanted - point.normalImpulse) * constraint.normal, arm);
          point.normalImpulse = wanted;
          load += wanted;
        }
      }

      return load;
    }

    void noteSupport(const Constraint& constraint)
    {
      float load = 0.0f;
      for (size_t i = 0; i < constraint.pointCount; ++i)
      {
        load += constraint.points[i].normalImpulse + constraint.points[i].pushImpulse;
      }

      if (load <= 0.0f)
      {
        return;
      }

      // A face, or the contact of a shape that rolls, holds a body still. Balanced on an edge or a corner it
      // is not at rest, however slowly it has started to fall.
      const bool steady = constraint.pointCount >= 3 || constraint.rollingGrip > 0.0f;
      noteSupport(m_bodies[constraint.body], m_bodies[constraint.other], constraint.normal, steady);
      noteSupport(m_bodies[constraint.other], m_bodies[constraint.body], -constraint.normal, steady);
    }

    // A contact pushing the body up holds it: it is no longer falling, and the flattest such contact is the
    // support it rests on, so a body in a valley rests on the gentler side.
    static void noteSupport(SolverBody& side, const SolverBody& support, const glm::vec3& normal, const bool steady)
    {
      if (!side.body || normal.y <= sideContactNormalY)
      {
        return;
      }

      side.body->setFalling(false);
      side.body->setNextFalling(false);
      if (normal.y > side.body->getNextSupportNormal().y)
      {
        side.body->setNextSupportNormal(normal);
        side.supported = steady;
        side.supportSpin = support.spin;
      }
    }

    [[nodiscard]] static ContactSolver::CachedContact cached(const Constraint& constraint)
    {
      const auto key = ContactSolver::keyOf(constraint.pair->collider, constraint.pair->other);
      const float side = key.first == constraint.pair->collider ? 1.0f : -1.0f;

      ContactSolver::CachedContact result;
      result.first = key.first;
      result.second = key.second;
      result.normal = constraint.normal * side;
      result.rollingImpulse = constraint.rollingImpulse * side;
      result.pointCount = static_cast<std::uint8_t>(constraint.pointCount);
      for (size_t i = 0; i < constraint.pointCount; ++i)
      {
        const auto& point = constraint.points[i];
        result.points[i] = { point.position, point.normalImpulse,
                             (point.tangentImpulse[0] * constraint.tangents[0] +
                              point.tangentImpulse[1] * constraint.tangents[1]) * side };
      }

      return result;
    }

    void write(const SolverBody& solved) const
    {
      auto& body = *solved.body;
      if (solved.velocity != body.getVelocity())
      {
        body.setVelocity(solved.velocity);
      }

      // The passes leave a resting body a trace of spin that only ever approaches zero. Too slow to turn it, it is
      // dropped, so the body comes exactly to rest on its support.
      auto angularVelocity = glm::degrees(solved.spin) / m_dt;
      const auto supportAngularVelocity = glm::degrees(solved.supportSpin) / m_dt;
      if (solved.supported && glm::length(angularVelocity - supportAngularVelocity) < PhysicsSystem::restAngularSpeed)
      {
        angularVelocity = supportAngularVelocity;
      }

      if (angularVelocity != body.getAngularVelocity())
      {
        body.setAngularVelocity(angularVelocity);
      }

      const auto& push = solved.push;
      const auto& turn = solved.turn;
      if (glm::dot(push, push) > minMove * minMove)
      {
        solved.transform->moveWorld(push);
      }

      const float angle = glm::length(turn);
      if (angle > minMove)
      {
        solved.transform->setWorldOrientation(glm::angleAxis(angle, turn / angle) *
                                              solved.transform->getOrientation());
      }
    }
  };
}

ContactSolver::PairKey ContactSolver::keyOf(const Collider* collider, const Collider* other)
{
  return std::less<const Collider*>{}(other, collider) ? PairKey{ other, collider } : PairKey{ collider, other };
}

void ContactSolver::solve(const std::span<const ContactPair> pairs, const Cache& previous,
                          std::vector<CachedContact>& resolved, const float dt)
{
  if (pairs.empty())
  {
    return;
  }

  Island island(pairs, previous, dt);
  island.solve();
  island.finish(resolved);
}
