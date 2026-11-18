#ifndef TERRAINSAVING_H
#define TERRAINSAVING_H

#include <filesystem>

class AssetRegistry;

namespace terrain
{
  class TerrainStore;
}

// The terrain step of an editor save, run before the project file is written. It refuses (false) while a terrain
// holds fewer chunks than its description says, since saving then would write a partial terrain. With an empty root
// it writes nothing and only warns when a terrain holds chunks. Otherwise it saves every terrain into root and fails
// on any error. Each outcome is logged.
[[nodiscard]] bool writeEditorTerrains(const std::filesystem::path& root, const AssetRegistry& registry,
                                       const terrain::TerrainStore& store);

#endif //TERRAINSAVING_H
