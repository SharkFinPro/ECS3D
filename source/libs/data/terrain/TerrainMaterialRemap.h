#ifndef TERRAINMATERIALREMAP_H
#define TERRAINMATERIALREMAP_H

#include "TerrainOps.h"
#include "TerrainTypes.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace terrain
{
  class TerrainVolume;

  // remap[old] is the material index a sample carrying old is given.
  using MaterialRemap = std::array<uint8_t, 256>;

  [[nodiscard]] constexpr MaterialRemap identityRemap()
  {
    MaterialRemap remap{};

    for (size_t index = 0; index < remap.size(); ++index)
    {
      remap[index] = static_cast<uint8_t>(index);
    }

    return remap;
  }

  [[nodiscard]] bool isIdentity(const MaterialRemap& remap);

  // The reserved material maps to itself and nothing else maps to it, so every remapped chunk can still be built.
  [[nodiscard]] bool isValidRemap(const MaterialRemap& remap);

  // The remap that keeps painted ground looking the same when palette entry removed is erased: its samples take
  // replacement (as indexed after the removal) and every later index moves down one. nullopt for a palette over
  // maxPaletteSize, an index outside it or a replacement equal to removed.
  [[nodiscard]] std::optional<MaterialRemap> remapForRemoval(size_t paletteSize, size_t removed, size_t replacement);

  // The remap that lets samples follow their material when moveMaterial moves entry from to to. nullopt for a palette
  // over maxPaletteSize or an index outside it; from == to is the identity.
  [[nodiscard]] std::optional<MaterialRemap> remapForMove(size_t paletteSize, size_t from, size_t to);

  // Remaps every sample of every held chunk, air samples included since a later Add can expose them, and returns new
  // chunks for only the chunks whose content changed, in z, y, x order. Distances are copied unchanged and the
  // volume is never mutated. An absent chunk is air the volume does not hold, so it stays absent. Empty for an
  // invalid or identity remap.
  [[nodiscard]] ChunkEdits remapMaterials(const TerrainVolume& volume, const MaterialRemap& remap);

  // Commits remapMaterials to the volume and returns the edits it applied.
  ChunkEdits commitRemap(TerrainVolume& volume, const MaterialRemap& remap);
}

#endif //TERRAINMATERIALREMAP_H
