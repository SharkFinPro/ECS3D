#ifndef TERRAINBRUSH_H
#define TERRAINBRUSH_H

#include "TerrainOps.h"
#include "TerrainTypes.h"
#include "TerrainVolume.h"
#include <glm/vec3.hpp>
#include <Protocol.h>
#include <cstdint>
#include <optional>

namespace terrain
{
  struct BrushOp
  {
    enum class Kind : uint8_t { add, subtract, smooth, flatten, paint };

    Kind kind = Kind::add;
    glm::vec3 center{ 0.0f };      // terrain-local world units
    float radius = 1.0f;           // terrain-local world units
    float strength = 1.0f;         // [0, 1]
    float falloff = 0.0f;          // [0, 1]
    uint8_t material = 0;
    glm::vec3 planePoint{ 0.0f };  // flatten only
    glm::vec3 planeNormal{ 0.0f, 1.0f, 0.0f };  // flatten only
  };

  inline constexpr float maxBrushRadiusVoxels = 64.0f;

  // A trilinear cell's corners reach up to sqrt(3) voxels from the surface, so every corner of every cell the surface
  // crosses must be rewritten.
  inline constexpr float brushBorderVoxels = 2.0f;

  // Every sample applyBrush can change lies inside the box. Both return empty results for a brush that is invalid
  // for the volume (non-finite field, radius beyond maxBrushRadiusVoxels, out-of-range value or a center too far out).
  [[nodiscard]] SampleBox dirtyBox(const TerrainVolume& volume, const BrushOp& op);

  // Pure: never mutates the volume, so the server and an editor prediction produce identical edits.
  [[nodiscard]] ChunkEdits applyBrush(const TerrainVolume& volume, const BrushOp& op);

  void pack(net::Message& message, const BrushOp& op);

  // Returns nullopt for a field a reader cannot trust. Underflow throws std::runtime_error, like every reader.
  [[nodiscard]] std::optional<BrushOp> unpackBrushOp(net::MessageReader& reader);
}

#endif //TERRAINBRUSH_H
