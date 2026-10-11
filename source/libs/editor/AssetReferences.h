#ifndef ASSETREFERENCES_H
#define ASSETREFERENCES_H

#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <uuid.h>

class AssetRegistry;
class Object;
class SceneManager;

// Whether any of the object's components mention the asset uuid in their serialized form. Searching the
// serialized form keeps this generic (no component type named here); only model/texture references -
// the ones that would dangle - ever match.
[[nodiscard]] bool objectReferencesAsset(const std::shared_ptr<Object>& object, const std::string& uuidString);

// Count the object nodes within a serialized prefab body (one Object tree) that reference the uuid.
[[nodiscard]] int countReferencesInPrefabNode(const nlohmann::json& node, const std::string& uuidString);

// How many objects reference an asset by uuid, for the delete-confirmation modal's warning. Scans the
// replicated scenes' objects and every prefab body - the two places an object tree lives editor-side.
[[nodiscard]] int countAssetReferences(const SceneManager& sceneManager, const AssetRegistry& assetRegistry,
                                       const uuids::uuid& assetUUID);

#endif //ASSETREFERENCES_H
