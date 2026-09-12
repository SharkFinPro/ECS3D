#ifndef WIRETYPES_H
#define WIRETYPES_H

#include <Protocol.h>

#include <glm/vec3.hpp>
#include <uuid.h>

// The aggregates this project sends across whole rather than a field at a time. Both are laid out as a
// plain run of same-sized members, so neither carries padding and neither holds a pointer - the two
// things net::wirePackable exists to keep off the wire. Anything else has to be packed field by field.
//
// The static_asserts are the claim, not decoration: glm::vec3 is only three tight floats while no
// GLM_FORCE_DEFAULT_ALIGNED_GENTYPES is defined, and building with it would quietly make the type
// alignas(16) and start broadcasting four bytes of stack residue per vector to every peer.
static_assert(sizeof(glm::vec3) == 3 * sizeof(float),
              "glm::vec3 is padded in this build; it cannot go on the wire whole");
static_assert(sizeof(uuids::uuid) == 16,
              "uuids::uuid is padded in this build; it cannot go on the wire whole");

namespace net {
  template <>
  inline constexpr bool wirePackable<glm::vec3> = true;

  template <>
  inline constexpr bool wirePackable<uuids::uuid> = true;
}

#endif //WIRETYPES_H
