#ifndef PROFILESTATS_H
#define PROFILESTATS_H

#include "History.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace profile {
  // Nearest rank: rank = ceil(p / 100 * n), 1-based, clamped to [1, n]. Empty input gives 0.
  [[nodiscard]] uint32_t percentile(std::vector<uint32_t> values, double p);

  // More than twice the median; a median of zero never counts.
  [[nodiscard]] bool isHitch(uint32_t micros, uint32_t medianMicros);

  struct Summary {
    uint32_t last = 0;
    double average = 0.0;
    uint32_t p95 = 0;
    uint32_t max = 0;
    std::size_t samples = 0;
  };

  // Over the newest `window` samples, or all of them when there are fewer. An empty history gives all zeros.
  [[nodiscard]] Summary summarizeWall(const History& history, std::size_t window);
  [[nodiscard]] Summary summarizeNode(const History& history, NodeId node, std::size_t window);

  // Wall time against the window's median.
  [[nodiscard]] std::size_t countHitches(const History& history, std::size_t window);
}

#endif
