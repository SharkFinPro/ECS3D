#ifndef TERRAINSAVING_H
#define TERRAINSAVING_H

#include <cstddef>
#include <filesystem>
#include <optional>
#include <uuid.h>

class AssetRegistry;

namespace terrain
{
  class TerrainStore;
  class TerrainVolume;
}

// The saved chunk count of a terrain the editor must not overwrite: the volume holds no chunks while the files under
// root name some, which is what the editor holds after loadProject resets the volumes. Empty when the terrain may be
// saved.
[[nodiscard]] std::optional<size_t> savedChunksToKeep(const std::filesystem::path& root, const uuids::uuid& uuid,
                                                      const terrain::TerrainVolume& volume);

// The terrain step of an editor save, run before the project file is written. It refuses (false) while a terrain
// holds fewer chunks than its description says, since saving then would write a partial terrain. With an empty root
// it writes nothing and only warns when a terrain holds chunks. Otherwise it saves every terrain into root and fails
// on any error, except that a terrain whose volume is empty over saved files holding chunks is left as it is with a
// warning. Each outcome is logged.
[[nodiscard]] bool writeEditorTerrains(const std::filesystem::path& root, const AssetRegistry& registry,
                                       const terrain::TerrainStore& store);

#endif //TERRAINSAVING_H
