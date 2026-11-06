#ifndef WORLDFACE_H
#define WORLDFACE_H

#include <glm/vec3.hpp>
#include <vector>

// One planar face of a convex shape in world space. The corners run counter-clockwise seen from outside, so the
// polygon is convex and its winding agrees with the outward unit normal. Plain geometry, so a collider can keep the
// faces the sim builds from it.
struct WorldFace {
  glm::vec3 normal{ 0.0f };
  glm::vec3 center{ 0.0f };
  std::vector<glm::vec3> corners;
};

#endif //WORLDFACE_H
