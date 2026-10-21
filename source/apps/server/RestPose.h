#ifndef RESTPOSE_H
#define RESTPOSE_H

#include "DefaultShapes.h"
#include <glm/vec3.hpp>

namespace defaultproject {

struct RestPose
{
  glm::vec3 rotation;
  float lowest;
};

// Turns a stable face of the hull to point straight down; lowest is then its lowest vertex at unit scale.
[[nodiscard]] RestPose restPose(const ConvexHull& hull);

}

#endif //RESTPOSE_H
