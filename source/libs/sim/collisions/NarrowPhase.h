#ifndef NARROWPHASE_H
#define NARROWPHASE_H

#include <glm/vec3.hpp>
#include <memory>
#include <optional>

class Collider;

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
// and the second shared, mirroring getSupport: it reads their geometry and owns neither.
[[nodiscard]] std::optional<Contact> findContact(Collider* collider, const std::shared_ptr<Collider>& other);

// GJK alone, for a caller that only needs to know whether the pair touches. Cheaper than findContact,
// and it answers yes in the degenerate case findContact reports as no contact - the pair does overlap
// there, it is the translation out of it that could not be built.
[[nodiscard]] bool intersects(Collider* collider, const std::shared_ptr<Collider>& other);

#endif //NARROWPHASE_H
