#ifndef BENCHSCENES_H
#define BENCHSCENES_H

#include "TestScene.h"
#include <cstdint>
#include <string>
#include <vector>

// Seeded collision workloads shared by the benchmark modes. Every scene sits on static geometry and falls
// under gravity, so the measured ticks include settling and resting contact, not only free flight.
namespace bench {
  struct SceneOptions {
    std::string name = "pile";
    // The scale knob each scene reads: a side length, a body count or a tile count, see describe().
    int size = 10;
    int layers = 4;
    uint32_t seed = 1;
  };

  // pile: size x size columns of unit boxes, `layers` high, spaced apart, dropping onto one big ground.
  // scatter: size * 100 boxes and spheres spread thinly over a wide area.
  // tiles: a size x size floor of separate static tiles with size * size / 4 bodies dropping onto it.
  // wall: a single-x wall of boxes, size wide and `layers` high, the case a minX sweep handles worst.
  // mixed: size * 100 bodies of mixed shapes and sizes in a walled arena with static clutter.
  [[nodiscard]] fixtures::Scene buildScene(const SceneOptions& options);

  [[nodiscard]] std::vector<std::string> sceneNames();
}

#endif //BENCHSCENES_H
