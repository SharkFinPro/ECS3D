#ifndef RECORDEDITS_H
#define RECORDEDITS_H

#include "EditCommand.h"
#include <nlohmann/json_fwd.hpp>
#include <optional>

class ObjectManager;

// Derives the EditCommand for an edit the editor is ABOUT to send, reading the before state from the
// editor's replicated view as it stands before that edit is applied or sent. Headless and side-effect
// free, so a mutation callback records what it sends without learning how a command is shaped.
//
// std::nullopt means nothing faithful can be recorded: an op nothing here handles, a uuid that will not
// parse, or a target the view does not have (a view a round trip behind the authority). The edit itself
// still goes out - only the history entry is skipped.
namespace edits {

[[nodiscard]] std::optional<EditCommand> commandForSceneEdit(const nlohmann::json& edit,
                                                             const ObjectManager& view);

// An addAsset over a uuid the registry already holds is a record replacement (a prefab body edit, or
// "Save as Prefab" over an existing name), so it derives a replaceAsset carrying the current record as
// its before state rather than an addAsset.
[[nodiscard]] std::optional<EditCommand> commandForAddAsset(const nlohmann::json& asset,
                                                            const AssetRegistry& view);

[[nodiscard]] std::optional<EditCommand> commandForRenameAsset(const nlohmann::json& op,
                                                               const AssetRegistry& view);

[[nodiscard]] std::optional<EditCommand> commandForRemoveAsset(const nlohmann::json& op,
                                                               const AssetRegistry& view);

}

#endif //RECORDEDITS_H
