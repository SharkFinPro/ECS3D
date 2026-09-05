#ifndef WIRETYPES_H
#define WIRETYPES_H

#include <Protocol.h>

#include <glm/vec3.hpp>
#include <uuid.h>

// The aggregates this project sends across whole rather than a field at a time. Both are laid out as a
// plain run of same-sized members, so neither carries padding and neither holds a pointer - the two
// things net::wirePackable exists to keep off the wire. Anything else has to be packed field by field.
namespace net {
  template <>
  inline constexpr bool wirePackable<glm::vec3> = true;

  template <>
  inline constexpr bool wirePackable<uuids::uuid> = true;
}

#endif //WIRETYPES_H
