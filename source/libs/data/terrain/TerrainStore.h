#ifndef TERRAINSTORE_H
#define TERRAINSTORE_H

#include "TerrainVolume.h"
#include <glm/vec3.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
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
    // Linear RGB in [0,1], drawn as the flat albedo when albedoTexture is nil; a textured material ignores it.
    glm::vec3 color{ 1.0f };

    bool operator==(const TerrainMaterial&) const = default;
  };

  struct TerrainDescription
  {
    uuids::uuid uuid;
    float voxelSize = 2.5f;
    std::vector<TerrainMaterial> palette;
    // The chunk count a snapshot carries so a receiver knows when it holds the whole terrain; the store does not
    // derive it.
    uint32_t chunkCount = 0;

    bool operator==(const TerrainDescription&) const = default;
  };

  inline constexpr float minVoxelSize = 1.0e-3f;
  inline constexpr float maxVoxelSize = 1000.0f;
  // The most materials the renderer can show at once.
  inline constexpr size_t maxPaletteSize = 16;
  inline constexpr size_t maxMaterialNameLength = 256;

  // Every component finite and in [0,1].
  [[nodiscard]] bool isValidColor(const glm::vec3& color);

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

    // 0 when the terrain is unknown or no revision is held for the coordinate.
    [[nodiscard]] uint32_t revision(const uuids::uuid& uuid, ChunkCoord coord) const;

    // The server's counter: returns the next revision for the terrain (first call 1), or 0 for an unknown uuid or a
    // counter already at its maximum.
    uint32_t nextRevision(const uuids::uuid& uuid);

    // The raw setter: sets the chunk (null or air erases it, as TerrainVolume::setChunk does) and records the given
    // revision even when it is lower than the one held; the higher-revision rule lives in applyTerrainChunks. The
    // revision is also recorded for an erased coordinate so a later lower-revision copy of the old chunk is refused,
    // and a revision above the counter raises it. False, with no change, for an unknown uuid, a revision of 0 or a
    // coordinate beyond maxChunkCoord.
    bool setChunk(const uuids::uuid& uuid, ChunkCoord coord, std::shared_ptr<const TerrainChunk> chunk,
                  uint32_t revision);

    // Gives every chunk the terrain's volume holds one fresh revision from nextRevision and returns it; 0 for an
    // unknown uuid or an exhausted counter (nothing is stamped then). The server calls it after loading terrain files so every loaded chunk has a revision above a
    // view's 0.
    uint32_t stampAllChunks(const uuids::uuid& uuid);

    // The server's record of a terrain whose chunks an editor is uploading. A terrain the store does not hold is
    // refused; a count of 0 clears the expectation. remove, reconcile and adopt forget it. resetVolumes keeps it: the
    // server sets expectations after its reset and a later reset must not lose them.
    bool expectChunks(const uuids::uuid& uuid, uint32_t count);

    [[nodiscard]] std::optional<uint32_t> expectedChunks(const uuids::uuid& uuid) const;

    void clearExpectation(const uuids::uuid& uuid);

    void clearExpectations();

  private:
    struct Entry
    {
      TerrainDescription description;
      std::shared_ptr<TerrainVolume> volume;
      // Describes the volume, so anything that replaces the volume clears it.
      std::unordered_map<ChunkCoord, uint32_t> revisions;
      // Survives a volume replacement so a revision handed out afterward is above every earlier one.
      uint32_t counter = 0;
      std::optional<uint32_t> expected;
    };

    std::unordered_map<uuids::uuid, Entry> m_terrains;
  };

  // The first terrain (in uuid order) still short of its expected chunk count, as "Play waits for terrain <uuid>: N of
  // M chunk(s) uploaded"; nullopt when no upload is pending.
  [[nodiscard]] std::optional<std::string> pendingUploadReason(const TerrainStore& store);

  struct IncompleteTerrain
  {
    uuids::uuid uuid;
    size_t held = 0;
    uint32_t expected = 0;
  };

  // The first terrain (in uuid order) whose volume holds fewer chunks than its description's chunk count.
  [[nodiscard]] std::optional<IncompleteTerrain> firstIncompleteTerrain(const TerrainStore& store);

  // The view side of the same gate: the first terrain (in uuid order) whose volume holds fewer chunks than its
  // description's chunk count, as "Play waits for terrain <uuid>: N of M chunk(s) received"; nullopt when none does.
  [[nodiscard]] std::optional<std::string> incompleteTerrainReason(const TerrainStore& store);
}

#endif //TERRAINSTORE_H
