#ifndef TERRAINOPS_H
#define TERRAINOPS_H

#include "TerrainChunk.h"
#include "TerrainTypes.h"
#include "TerrainVolume.h"
#include <glm/vec3.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace terrain
{
  using ChunkEdits = std::vector<std::pair<ChunkCoord, std::shared_ptr<const TerrainChunk>>>;

  // Applies every edit; a null chunk means air. Validates every coordinate first and throws std::out_of_range before
  // changing anything, so a refused batch leaves the volume untouched.
  void commit(TerrainVolume& volume, const ChunkEdits& edits);

  inline constexpr int64_t maxEditChunks = 16384;

  // Sample coordinates are written in place; distance and material arrive holding the input volume's values.
  using SampleEdit = std::function<void(glm::ivec3 sample, int16_t& distance, uint8_t& material)>;

  // Rebuilds every chunk the box touches and returns only the chunks whose content changed; a result that is air is
  // emitted as nullptr. A distance written outside the band is clamped, and the reserved material keeps the input's.
  // Returns empty edits for an empty box, a box over maxEditChunks chunks or a box with a chunk outside
  // isValidChunkCoord. Never mutates the volume.
  [[nodiscard]] ChunkEdits editSamples(const TerrainVolume& volume, const SampleBox& box, const SampleEdit& edit);

  // One maximum brush box (160 samples per axis) with its one-sample margin; callers split larger regions so a single
  // redistance stays a bounded synchronous cost.
  inline constexpr int64_t maxRedistanceSamples = 160ll * 160ll * 160ll;

  // Restores a distance-like slope inside the box without moving the surface. Returns empty edits for an empty box,
  // a box editSamples refuses, or a box whose grown grid (the box plus a one-sample margin per side) exceeds
  // maxRedistanceSamples.
  [[nodiscard]] ChunkEdits redistance(const TerrainVolume& volume, const SampleBox& box);
}

#endif //TERRAINOPS_H
