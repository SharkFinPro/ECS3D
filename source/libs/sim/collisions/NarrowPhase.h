#ifndef NARROWPHASE_H
#define NARROWPHASE_H

#include <glm/vec3.hpp>
#include <memory>
#include <optional>

class Collider;

// Namespaced, unlike getSupport beside it: Contact, findContact and intersects are generic enough names
// that leaving them global in a header CollisionSystem.h drags into the apps would be asking for a
// collision later. Nothing here is reachable by argument-dependent lookup, so callers qualify.
namespace collisions {
  // What the narrow phase found where two colliders overlap, from the first collider's point of view.
  struct Contact {
    // Moves the first collider clear of the second: its direction is the contact normal and its length
    // the penetration depth. Never the zero vector - a degenerate result is reported as no contact at
    // all rather than as a contact with nothing to resolve.
    glm::vec3 minimumTranslationVector;

    // A point on the overlap, in world space.
    glm::vec3 point;

    [[nodiscard]] float depth() const;

    // Unit length, pointing the way the first collider has to move.
    [[nodiscard]] glm::vec3 normal() const;
  };

  // GJK for the overlap, then EPA for the depth, normal and contact point. Takes the first collider raw
  // and the second shared, the way getSupport does: it reads their geometry and owns neither.
  [[nodiscard]] std::optional<Contact> findContact(Collider* collider, const std::shared_ptr<Collider>& other);

  // For a caller that only needs to know whether the pair touches, which is what the sweep asks. It
  // skips the work findContact does to describe the overlap - EPA on a general pair, the contact point
  // on a pair of spheres.
  //
  // The two agree except in one case: on the general path this answers yes for a pair whose translation
  // out of the overlap EPA could not build, which findContact reports as no contact at all.
  [[nodiscard]] bool intersects(Collider* collider, const std::shared_ptr<Collider>& other);
}

#endif //NARROWPHASE_H
