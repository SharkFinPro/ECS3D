#ifndef TESTPRINTERS_H
#define TESTPRINTERS_H

#include <glm/vec3.hpp>
#include <ostream>

// Without this GoogleTest falls back to a hex dump of the bytes, which makes a failed vector
// comparison unreadable. Found by ADL, so it has to live in glm's namespace.
namespace glm {
  inline void PrintTo(const vec3& value, std::ostream* out)
  {
    *out << "(" << value.x << ", " << value.y << ", " << value.z << ")";
  }
}

#endif //TESTPRINTERS_H
