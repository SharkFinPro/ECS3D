#ifndef DEFAULTTERRAINNOISE_H
#define DEFAULTTERRAINNOISE_H

#include <glm/common.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

// The deterministic value noise the built-in terrains are shaped with.
namespace defaultterrain
{
  inline uint32_t hashCell(const int x, const int z, const uint32_t seed)
  {
    uint32_t value =
      static_cast<uint32_t>(x) * 0x8DA6B343u ^ static_cast<uint32_t>(z) * 0xD8163841u ^ seed * 0xCB1AB31Fu;
    value ^= value >> 15;
    value *= 0x2C1B3C6Du;
    value ^= value >> 12;
    value *= 0x297A2D39u;
    value ^= value >> 15;

    return value;
  }

  inline float easeCurve(const float t)
  {
    return t * t * (3.0f - 2.0f * t);
  }

  inline float easeBetween(const float low, const float high, const float value)
  {
    return easeCurve(std::clamp((value - low) / (high - low), 0.0f, 1.0f));
  }

  // In [-1, 1].
  inline float valueNoise(const float x, const float z, const uint32_t seed)
  {
    const int cellX = static_cast<int>(std::floor(x));
    const int cellZ = static_cast<int>(std::floor(z));
    const float fractionX = easeCurve(x - static_cast<float>(cellX));
    const float fractionZ = easeCurve(z - static_cast<float>(cellZ));

    const auto corner = [seed](const int cx, const int cz)
    {
      return static_cast<float>(hashCell(cx, cz, seed) & 0xFFFFu) / 32767.5f - 1.0f;
    };

    const float lower = glm::mix(corner(cellX, cellZ), corner(cellX + 1, cellZ), fractionX);
    const float upper = glm::mix(corner(cellX, cellZ + 1), corner(cellX + 1, cellZ + 1), fractionX);

    return glm::mix(lower, upper, fractionZ);
  }

  inline float fbm(const float x, const float z)
  {
    return (valueNoise(x / 64.0f, z / 64.0f, 1) + 0.5f * valueNoise(x / 32.0f, z / 32.0f, 2) +
            0.25f * valueNoise(x / 16.0f, z / 16.0f, 3)) / 1.75f;
  }

  inline float distance2D(const float x, const float z, const float centerX, const float centerZ)
  {
    return std::hypot(x - centerX, z - centerZ);
  }
}

#endif //DEFAULTTERRAINNOISE_H
