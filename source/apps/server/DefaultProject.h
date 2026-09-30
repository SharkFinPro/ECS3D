#ifndef DEFAULTPROJECT_H
#define DEFAULTPROJECT_H

#include <nlohmann/json_fwd.hpp>
#include <cstdint>
#include <optional>

// The layout of Scene 3's tower. The defaults are the built-in scene: a random seed each launch, and
// bodies that may start inside a neighbor.
struct Scene3Options {
  // Unset seeds from std::random_device. A seed also makes the object uuids repeatable.
  std::optional<uint32_t> seed;
  int gridSize = 6;
  int layerCount = 15;

  // Widens the spacing so no two bodies' bounding boxes overlap at the start, whatever they are drawn as.
  bool overlapFree = false;
};

// Builds the built-in sample project as a project blob (the same JSON shape ProjectSerializer loads,
// so it can just be deserialize()'d). Scene 3 and the falling-balls scene are generated procedurally
// (random placement), which a static file can't capture.
[[nodiscard]] nlohmann::json buildDefaultProject(const Scene3Options& scene3 = {});

#endif //DEFAULTPROJECT_H
