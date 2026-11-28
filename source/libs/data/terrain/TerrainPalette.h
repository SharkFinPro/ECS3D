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

  // Every material index a solid sample (distance <= 0) of any held chunk carries, since the surface takes its
  // material from the solid side. An absent chunk is air and contributes nothing. It reads every held chunk, so
  // call it on demand (a remove click), not per frame.
  [[nodiscard]] std::bitset<256> materialsInUse(const TerrainVolume& volume);

  // The edits below change a palette copy. Each returns nullopt when it applied, or the reason it was refused, with the
  // palette unchanged.
  std::optional<std::string> addMaterial(std::vector<TerrainMaterial>& palette, TerrainMaterial material);

  // Refused when any sample uses the index or a later one, since removing shifts every later entry down and would
  // re-skin painted ground.
  std::optional<std::string> removeMaterial(std::vector<TerrainMaterial>& palette, size_t index,
                                            const std::bitset<256>& inUse);

  // Removes the entry whatever the ground carries, for a removal whose painted samples a remapForRemoval has already
  // moved off it and its later entries. Refused only for an index out of range.
  std::optional<std::string> eraseMaterial(std::vector<TerrainMaterial>& palette, size_t index);

  std::optional<std::string> renameMaterial(std::vector<TerrainMaterial>& palette, size_t index,
                                            const std::string& name);

  // Painted samples keep their index, so they show whichever entry now sits there.
  std::optional<std::string> moveMaterial(std::vector<TerrainMaterial>& palette, size_t from, size_t to);

  // Refused for an index out of range or a component that is not finite and inside [0,1].
  std::optional<std::string> setMaterialColor(std::vector<TerrainMaterial>& palette, size_t index,
                                              const glm::vec3& color);

  std::optional<std::string> setMaterialTextures(std::vector<TerrainMaterial>& palette, size_t index,
                                                 const uuids::uuid& albedo, const uuids::uuid& specular);
}

#endif //TERRAINPALETTE_H
