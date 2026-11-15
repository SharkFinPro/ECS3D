#ifndef TERRAINSTORE_H
#define TERRAINSTORE_H

#include "TerrainVolume.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <uuid.h>
#include <vector>

class AssetRegistry;

namespace terrain
{
  struct TerrainMaterial
  {
    std::string name;
    // A nil uuid means no texture.
    uuids::uuid albedoTexture;
    uuids::uuid specularTexture;
  };

  struct TerrainDescription
  {
    uuids::uuid uuid;
    float voxelSize = 2.5f;
    std::vector<TerrainMaterial> palette;
    // The chunk count a snapshot carries so a receiver knows when it holds the whole terrain; the store does not
    // derive it.
    uint32_t chunkCount = 0;
  };

  inline constexpr float minVoxelSize = 1.0e-3f;
  inline constexpr float maxVoxelSize = 1000.0f;
  inline constexpr size_t maxPaletteSize = 255;
  inline constexpr size_t maxMaterialNameLength = 256;

  [[nodiscard]] bool isValidDescription(const TerrainDescription& description);

  // Not thread safe.
  class TerrainStore
  {
  public:
    // Returns the new volume, or nullptr when the description is invalid or the uuid is already held.
    std::shared_ptr<TerrainVolume> create(const TerrainDescription& description);

    [[nodiscard]] std::shared_ptr<TerrainVolume> find(const uuids::uuid& uuid) const;

    // Valid until the next remove, create, reconcile, adopt or resetVolumes.
    [[nodiscard]] const TerrainDescription* description(const uuids::uuid& uuid) const;

    // Installs a loaded terrain, replacing any entry with the same uuid. False, with no change, for an invalid
    // description, a null volume or a volume whose voxel size differs from the description's.
    bool adopt(const TerrainDescription& description, std::shared_ptr<TerrainVolume> volume);

    bool remove(const uuids::uuid& uuid);

    // Replaces the description of a held terrain. The volume is kept while the voxel size is unchanged; a new voxel
    // size means different data, so it gets a fresh empty volume. False, with no change, for an invalid description
    // or an unknown uuid.
    bool applyDescription(const TerrainDescription& description);

    // Gives every held terrain a fresh empty volume of its description's voxel size and keeps the descriptions.
    void resetVolumes();

    // Sorted, so callers that pack or log get a deterministic order.
    [[nodiscard]] std::vector<uuids::uuid> terrains() const;

    // Drops terrains whose Terrain record is gone and creates an empty default-description terrain for each new
    // Terrain record. Volumes of terrains that stay are kept (same pointer).
    void reconcile(const AssetRegistry& registry);

  private:
    struct Entry
    {
      TerrainDescription description;
      std::shared_ptr<TerrainVolume> volume;
    };

    std::unordered_map<uuids::uuid, Entry> m_terrains;
  };
}

#endif //TERRAINSTORE_H
