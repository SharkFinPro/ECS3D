#ifndef TERRAINPALETTE_H
#define TERRAINPALETTE_H

#include "TerrainStore.h"
#include <bitset>
#include <cstddef>
#include <optional>
#include <string>
#include <uuid.h>
#include <vector>

namespace terrain
{
  class TerrainVolume;

  // Every material index a sample of any held chunk carries. An absent chunk is air and contributes nothing.
  [[nodiscard]] std::bitset<256> materialsInUse(const TerrainVolume& volume);

  // The edits below change a palette copy. Each returns nullopt when it applied, or the reason it was refused, with the
  // palette unchanged.
  std::optional<std::string> addMaterial(std::vector<TerrainMaterial>& palette, TerrainMaterial material);

  // Refused when any sample uses the index or a later one, since removing shifts every later entry down and would
  // re-skin painted ground.
  std::optional<std::string> removeMaterial(std::vector<TerrainMaterial>& palette, size_t index,
                                            const std::bitset<256>& inUse);

  std::optional<std::string> renameMaterial(std::vector<TerrainMaterial>& palette, size_t index,
                                            const std::string& name);

  // Painted samples keep their index, so they show whichever entry now sits there.
  std::optional<std::string> moveMaterial(std::vector<TerrainMaterial>& palette, size_t from, size_t to);

  std::optional<std::string> setMaterialTextures(std::vector<TerrainMaterial>& palette, size_t index,
                                                 const uuids::uuid& albedo, const uuids::uuid& specular);
}

#endif //TERRAINPALETTE_H
