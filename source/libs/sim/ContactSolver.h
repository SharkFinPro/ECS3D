#ifndef CONTACTSOLVER_H
#define CONTACTSOLVER_H

#include "collisions/NarrowPhase.h"
#include <glm/vec3.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

class Collider;
class RigidBody;

// One touching pair of colliders. contact is from collider's point of view: its normal pushes body away from
// otherBody. Either body is null for static geometry, never both.
struct ContactPair {
  RigidBody* body = nullptr;
  RigidBody* otherBody = nullptr;
  Collider* collider = nullptr;
  Collider* other = nullptr;
  collisions::Contact contact;
  // The displacement of collider the contact was asked for with, or zero when it was measured as the pair stands.
  glm::vec3 sweep{ 0 };
};

// Resolves the contacts of one island together: every contact point is a constraint on the relative velocity of
// its two bodies, and the constraints are relaxed in turn over several passes, each keeping the total impulse it
// has applied so a later pass can take some of it back. A stack or a pile then shares its load through all of its
// contacts at once rather than one body at a time.
class ContactSolver {
public:
  // The impulses a pair ended a tick with. The next tick starts from them, since a resting contact needs nearly
  // the same ones again.
  struct CachedPoint {
    glm::vec3 position{ 0 };
    float normalImpulse = 0.0f;
    // On the first collider's body, in world axes.
    glm::vec3 frictionImpulse{ 0 };
  };

  struct CachedContact {
    const Collider* first = nullptr;
    const Collider* second = nullptr;
    glm::vec3 normal{ 0 };
    glm::vec3 rollingImpulse{ 0 };
    std::array<CachedPoint, 4> points{};
    std::uint8_t pointCount = 0;
  };

  struct PairKey {
    const Collider* first;
    const Collider* second;

    bool operator==(const PairKey&) const = default;
  };

  struct PairKeyHash {
    [[nodiscard]] size_t operator()(const PairKey& key) const noexcept
    {
      return std::hash<const Collider*>{}(key.first) * 31 + std::hash<const Collider*>{}(key.second);
    }
  };

  using Cache = std::unordered_map<PairKey, CachedContact, PairKeyHash>;

  // The key a pair is cached under, whichever collider comes first.
  [[nodiscard]] static PairKey keyOf(const Collider* collider, const Collider* other);

  // Changes the velocities of the pairs' bodies so no contact closes, with friction, and moves the bodies out of
  // overlap. previous is only read; the impulses this solve ends with are appended to resolved.
  static void solve(std::span<const ContactPair> pairs, const Cache& previous, std::vector<CachedContact>& resolved,
                    float dt);
};

#endif //CONTACTSOLVER_H
