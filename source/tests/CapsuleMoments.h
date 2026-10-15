#ifndef CAPSULEMOMENTS_H
#define CAPSULEMOMENTS_H

#include <glm/gtc/constants.hpp>

namespace capsuleMoments {
  // Textbook principal moments of a solid capsule per unit mass: a cylinder of height cylinderHeight capped by
  // two hemispheres, about its axis and about an axis across it through the center.
  struct Moments
  {
    float aboutAxis;
    float acrossAxis;
  };

  inline Moments textbook(const float radius, const float cylinderHeight)
  {
    const float pi = glm::pi<float>();
    const float cylinder = pi * radius * radius * cylinderHeight;
    const float hemisphere = 2.0f / 3.0f * pi * radius * radius * radius;
    const float mass = cylinder + 2.0f * hemisphere;
    const float r2 = radius * radius;
    const float h2 = cylinderHeight * cylinderHeight;

    const float aboutAxis = cylinder * r2 / 2.0f + 2.0f * hemisphere * 0.4f * r2;
    const float acrossAxis = cylinder * (r2 / 4.0f + h2 / 12.0f) +
                             2.0f * hemisphere * (0.4f * r2 + h2 / 4.0f + 3.0f * cylinderHeight * radius / 8.0f);

    return { aboutAxis / mass, acrossAxis / mass };
  }

}

#endif //CAPSULEMOMENTS_H
