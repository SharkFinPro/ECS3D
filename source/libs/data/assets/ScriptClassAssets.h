#ifndef SCRIPTCLASSASSETS_H
#define SCRIPTCLASSASSETS_H

#include <cstddef>
#include <string>
#include <vector>
#include <uuid.h>

class AssetRegistry;

// The uuid every machine derives for a script class of this name, so a record registered here is the same
// across runs and servers.
[[nodiscard]] uuids::uuid scriptClassUuid(const std::string& className);

// Registers a Script record for each class name no Script record names yet. The record has no source path,
// since the compiler does not say which file a class came from. Returns how many records were added.
size_t registerScriptClassAssets(AssetRegistry& registry, const std::vector<std::string>& classNames);

#endif //SCRIPTCLASSASSETS_H
