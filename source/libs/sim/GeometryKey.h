#ifndef GEOMETRYKEY_H
#define GEOMETRYKEY_H

#include <cstdint>

class Object;

// The sum of the update ids of the object's Transform and of every ancestor its world placement is combined
// with. Update ids only ever increase, so the sum changes whenever any of them does. Zero without a Transform.
[[nodiscard]] uint64_t geometryKeyOf(const Object& object);

#endif //GEOMETRYKEY_H
