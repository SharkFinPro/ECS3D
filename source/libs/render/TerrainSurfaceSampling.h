#ifndef TERRAINSURFACESAMPLING_H
#define TERRAINSURFACESAMPLING_H

#include <terrain/TerrainTypes.h>
#include <terrain/TerrainVolume.h>
#include <cstdint>
#include <span>
#include <vector>

namespace terrain
{
  // Surface bounds are rounded out to this many samples so a terrain that grows by a chunk does not always need a new
  // surface.
  inline constexpr int surfaceBoundsStep = 2 * chunkSize;

  // The renderer's cells per surface chunk; also the cap below is counted in these.
  inline constexpr int surfaceChunkCells = 32;

  // More surface chunks than this is a terrain too spread out to draw as one surface (each is extracted on creation).
  inline constexpr uint64_t maxSurfaceChunks = 32768;

  // Bounds for a surface over the occupied box: one air sample beyond it on every side, then rounded out to
  // multiples of surfaceBoundsStep. Empty for an empty box.
  [[nodiscard]] SampleBox surfaceBounds(const SampleBox& occupied);

  // True when inner is empty or lies entirely inside outer.
  [[nodiscard]] bool containsBox(const SampleBox& outer, const SampleBox& inner);

  // Surface chunks a box of samples splits into: an axis of n samples has n - 1 cells, ceil(cells / surfaceChunkCells)
  // chunks; 0 when any axis has fewer than two samples. Saturates at the uint64_t maximum instead of overflowing.
  [[nodiscard]] uint64_t surfaceChunkCount(const SampleBox& bounds);

  // Coordinates whose chunk differs between the two maps (pointer differs, or held in only one), sorted by z, then y,
  // then x so callers and tests get a deterministic order.
  [[nodiscard]] std::vector<ChunkCoord> changedChunks(const TerrainVolume::ChunkMap& before,
                                                      const TerrainVolume::ChunkMap& after);

  // The smallest sample box covering every listed chunk's samples; empty for none.
  [[nodiscard]] SampleBox chunkSampleBounds(std::span<const ChunkCoord> chunks);

  // Writes the region's samples from the snapshot, x-fastest from region.min: distance in voxels (decoded), material
  // as stored; a chunk the map lacks reads as air with material 0. At level of detail lod the sample at region offset
  // p is the terrain sample (region.min + p) * 2^lod. False, writing nothing, when the region is empty, lod > 8, or
  // either span's size differs from the region's sample count.
  [[nodiscard]] bool fillSurfaceSamples(const TerrainVolume::ChunkMap& chunks, const SampleBox& region, uint32_t lod,
                                        std::span<float> distance, std::span<uint8_t> material);
}

#endif //TERRAINSURFACESAMPLING_H
