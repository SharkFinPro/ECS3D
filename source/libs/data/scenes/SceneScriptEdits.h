#ifndef SCENESCRIPTEDITS_H
#define SCENESCRIPTEDITS_H

#include "Replication.h"
#include <nlohmann/json_fwd.hpp>
#include <cstddef>
#include <optional>
#include <string>

class SceneAsset;

// The sceneEdit ops that change a scene's ordered script list. They carry no scene uuid, like every other
// sceneEdit op: the server applies them to its current scene. They are not ObjectManager ops, so
// applySceneEdit refuses them as malformed - which is also what a batch containing one gets, since a batch
// is applied by applySceneEdit. The server routes them here instead, checking isSceneScriptEdit first.
//
// A scene holds at most one script per class name, so a class names its entry. Results reuse
// SceneEditResult: a class that is not in the list is unknownComponent (the entry is the thing being
// addressed, as a component is for an object), and a well formed op the list refuses - adding a class
// already present, or a move that changes nothing or lands past the end - is rejected.
namespace replication {

// { "op": "addSceneScript", "className": s, "index"?: n, "fields"?: [...] }. An index past the end appends.
// The optional fields blob is loaded into the new entry (undo of a removal restores the saved values).
[[nodiscard]] nlohmann::json buildAddSceneScript(const std::string& className,
                                                 std::optional<std::size_t> index = std::nullopt,
                                                 const nlohmann::json* fields = nullptr);

[[nodiscard]] nlohmann::json buildRemoveSceneScript(const std::string& className);

// The index is read against the list with the script removed, the way SceneAsset::moveScript reads it.
[[nodiscard]] nlohmann::json buildReorderSceneScript(const std::string& className, std::size_t index);

[[nodiscard]] nlohmann::json buildSetSceneScriptFields(const std::string& className,
                                                       const nlohmann::json& fields);

[[nodiscard]] bool isSceneScriptEdit(const nlohmann::json& edit);

SceneEditResult applySceneScriptEdit(SceneAsset& scene, const nlohmann::json& edit);

// An array whose entries are objects with a string "name" and "type". Values are not inspected: the live
// instance validates them against its exposed fields.
[[nodiscard]] bool isValidSceneScriptFields(const nlohmann::json& fields);

// Position of the class in the scene's script list, nullopt when absent.
[[nodiscard]] std::optional<std::size_t> sceneScriptIndex(const SceneAsset& scene, const std::string& className);

}

#endif //SCENESCRIPTEDITS_H
