#ifndef SLEEPING_H
#define SLEEPING_H

#include <cstdint>

// Untuned starting values; a resting body has to stay under both for ticksToSleep ticks in a row.
namespace sleeping {
  // Units per tick, like the velocity it is compared with.
  constexpr float linearSleepSpeed = 0.004f;

  // Degrees per second, like the angular velocity it is compared with.
  constexpr float angularSleepSpeed = 3.0f;

  // Half a second at 50 Hz.
  constexpr uint32_t ticksToSleep = 25;

  // A body pushed out of a contact whose unit normal has at least this much y is resting on it.
  constexpr float supportNormalY = 0.5f;

  // Grounded mode: a body touching a sleeper leaves it asleep unless it moves faster than this (per tick).
  constexpr float gentleContactSpeed = 0.02f;
}

#endif //SLEEPING_H
