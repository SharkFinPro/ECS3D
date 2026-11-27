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

  // Edits one row of samples in place: first is the row's first sample, count its length, and distances and materials
  // point at the row's values (input on entry). Returns whether it may have changed any. It must leave every distance
  // within -airDistance..airDistance and never write reservedMaterial.
  using RowEdit = std::function<bool(glm::ivec3 first, int count, int16_t* distances, uint8_t* materials)>;

  // Asked for a chunk the box covers whole: returns true with the one distance and material the edit would write to
  // every sample of it, so the samples are never visited. It must match the edit exactly.
  using UniformChunkHint = std::function<bool(ChunkCoord coord, int16_t& distance, uint8_t& material)>;

  // Asked for each chunk the box touches with the samples of it the box covers: returns true when the edit leaves every
  // one of them unchanged, so the chunk is not rebuilt at all. It must never answer true for a sample the edit changes.
  using ChunkSkip = std::function<bool(const SampleBox& covered)>;

  // Rebuilds every chunk the box touches and returns only the chunks whose content changed; a result that is air is
  // emitted as nullptr. A distance written outside the band is clamped, and the reserved material keeps the input's.
  // Returns empty edits for an empty box, a box over maxEditChunks chunks or a box with a chunk outside
  // isValidChunkCoord. Never mutates the volume. With parallel set, chunks are edited on parallelFor's pool, so edit,
  // uniformHint and skipChunk must be safe to call concurrently; the result is the same either way.
  [[nodiscard]] ChunkEdits editSamples(const TerrainVolume& volume, const SampleBox& box, const SampleEdit& edit,
                                       const UniformChunkHint& uniformHint = {},
                                       const ChunkSkip& skipChunk = {}, bool parallel = false);

  // editSamples over whole rows, for an edit that can skip most of a row or do its arithmetic in a tight loop. The
  // other parameters and the result are as for editSamples; a row edit that reports no change leaves its row as it was.
  [[nodiscard]] ChunkEdits editRows(const TerrainVolume& volume, const SampleBox& box, const RowEdit& editRow,
                                    const UniformChunkHint& uniformHint = {}, const ChunkSkip& skipChunk = {},
                                    bool parallel = false);

  // One maximum brush box (160 samples per axis) with its one-sample margin; callers split larger regions so a single
  // redistance stays a bounded synchronous cost.
  inline constexpr int64_t maxRedistanceSamples = 160ll * 160ll * 160ll;

  // Restores a distance-like slope inside the box without moving the surface. Returns empty edits for an empty box,
  // a box editSamples refuses, or a box whose grown grid (the box plus a one-sample margin per side) exceeds
  // maxRedistanceSamples.
  [[nodiscard]] ChunkEdits redistance(const TerrainVolume& volume, const SampleBox& box);
}

#endif //TERRAINOPS_H
