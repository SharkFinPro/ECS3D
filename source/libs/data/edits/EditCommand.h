#ifndef EDITCOMMAND_H
#define EDITCOMMAND_H

#include "assets/AssetRegistry.h"
#include <nlohmann/json_fwd.hpp>
#include <cstddef>
#include <optional>
#include <string>
#include <uuid.h>
#include <variant>

class ObjectManager;

namespace net {
  class Message;
}

// A command per editor mutation, and the undo/redo stack that replays them as ordinary edits sent back
// to the authoritative server - undo is a new edit, never a local rewind. See AGENTS.md's replication
// section for why: the server stays the single source of truth, so "undo" has to mean "send the reverse
// edit and wait for the rebroadcast" like every other change, or two connected editors would disagree
// about what undo even means.
namespace edits {

enum class CommandKind {
  componentEdit,
  addObject,
  removeObject,
  reparentObject,
  reorderObject,
  renameObject,
  addComponent,
  removeComponent,
  duplicateObject,
  instantiatePrefab,
  addAsset,
  replaceAsset,
  renameAsset,
  removeAsset
};

// Whether a command's undo/redo payload is a structural sceneEdit op (nlohmann::json, built by
// Replication.h's build* functions and sent as a sceneEdit message by the caller) or an already
// wire-ready net::Message (editComponent/addAsset/replaceAsset/renameAsset/removeAsset, built by
// Replication.h's pack* functions). Fixed per kind, so a caller branches on this rather than guessing
// which accessor to call.
enum class PayloadForm {
  sceneEdit,
  networkMessage
};

// Why an undo/redo did not hand back a payload. Kept distinct rather than a bool, the same reasoning as
// Replication.h's SceneEditResult/ComponentEditResult: the caller has to say something different to the
// user for each - notUndoable is a limitation of this command's kind, not a conflict; targetMissing/
// targetChanged name a real divergence between the recorded state and the live scene, which the caller
// can report by uuid.
enum class ValidationFailure {
  none,
  notUndoable,
  targetMissing,
  targetChanged
};

struct Validation {
  ValidationFailure failure = ValidationFailure::none;
  uuids::uuid conflict{}; // meaningful only when failure != none - which object/asset conflicted

  [[nodiscard]] bool ok() const
  {
    return failure == ValidationFailure::none;
  }
};

// One editor mutation: what kind it was, the uuid(s) it targets, and its before/after state in
// replicated form (a component's serialize() blob, an object's name/parent, a whole removed subtree,
// etc). Deliberately a plain value - copyable and comparable - rather than something holding a built
// net::Message: a Message has no operator== and packing one at record time would throw away the
// structured fields validation needs to read back later. The payload actually sent is built on demand
// (buildUndo*/buildRedo*) from the same Replication.h builders the editor would have called by hand.
//
// JSON blobs are stored dumped (std::string), not as nlohmann::json members, so this header only needs
// json_fwd.hpp - the same reason AssetRecord::body and Script::m_fields are strings rather than json.
class EditCommand {
public:
  [[nodiscard]] static EditCommand componentEdit(const uuids::uuid& objectUUID,
                                                 const nlohmann::json& before,
                                                 const nlohmann::json& after);

  [[nodiscard]] static EditCommand addObject(const uuids::uuid& objectUUID,
                                             const std::optional<uuids::uuid>& parentUUID,
                                             std::string name,
                                             std::size_t siblingIndex);

  // Not reversible (see isReversible) but still representable: removedSubtree is the removed object's
  // full Object::serialize() blob, kept in case a future piece gains a way to replay it.
  [[nodiscard]] static EditCommand removeObject(const uuids::uuid& objectUUID,
                                                const std::optional<uuids::uuid>& parentUUID,
                                                std::size_t siblingIndex,
                                                const nlohmann::json& removedSubtree);

  [[nodiscard]] static EditCommand reparentObject(const uuids::uuid& objectUUID,
                                                  const std::optional<uuids::uuid>& beforeParentUUID,
                                                  const std::optional<uuids::uuid>& afterParentUUID);

  // Drop-BETWEEN-siblings: unlike reparentObject (which always appends, so only the parent is worth
  // recording) this also carries the sibling index on each side, since the whole point of the edit can be
  // a same-parent move.
  [[nodiscard]] static EditCommand reorderObject(const uuids::uuid& objectUUID,
                                                 const std::optional<uuids::uuid>& beforeParentUUID,
                                                 std::size_t beforeIndex,
                                                 const std::optional<uuids::uuid>& afterParentUUID,
                                                 std::size_t afterIndex);

  [[nodiscard]] static EditCommand renameObject(const uuids::uuid& objectUUID,
                                                std::string beforeName,
                                                std::string afterName);

  [[nodiscard]] static EditCommand addComponent(const uuids::uuid& objectUUID,
                                                std::string registryKey,
                                                std::string className = {});

  // removedComponent is the removed component's serialize() blob; registryKey/className are derived from
  // it the same way componentEdit derives them. Undo puts it back with an addComponent that carries this
  // blob as "data" - see Replication.h's buildAddComponent.
  [[nodiscard]] static EditCommand removeComponent(const uuids::uuid& objectUUID,
                                                   const nlohmann::json& removedComponent);

  // parentUUID/siblingIndex are the source's own position at record time (duplicateObject always lands
  // the copy as the source's next sibling) - informational only, not enforced on redo, the same as
  // addObject's siblingIndex.
  [[nodiscard]] static EditCommand duplicateObject(const uuids::uuid& sourceUUID,
                                                   const uuids::uuid& duplicateUUID,
                                                   const std::optional<uuids::uuid>& parentUUID,
                                                   std::size_t siblingIndex);

  // siblingIndex is informational only, not enforced on redo, the same as addObject's. prefabBody is the
  // prefab asset's body at record time (AssetRecord::body, the raw string - never parsed here, the same
  // way ReplaceAssetData carries its before/after bodies): redo re-instantiates from whatever the
  // registry holds for prefabUUID now, and the registry lets a prefab body be replaced in place (a save
  // over the same uuid), so validateForRedo compares this against the live record to refuse a redo that
  // would instantiate a body the user never actually duplicated/instantiated from.
  [[nodiscard]] static EditCommand instantiatePrefab(const uuids::uuid& prefabUUID,
                                                     const uuids::uuid& instanceUUID,
                                                     const std::optional<uuids::uuid>& parentUUID,
                                                     std::size_t siblingIndex, std::string prefabBody);

  [[nodiscard]] static EditCommand addAsset(const uuids::uuid& assetUUID, AssetType type,
                                            std::string path, std::string className, std::string body);

  // Re-registering an existing uuid with a new record: a prefab body edit, or "Save as Prefab" over a
  // name that already exists. Distinct from addAsset because addAsset's reverse is a removeAsset, which
  // would delete the asset rather than put its previous body back.
  [[nodiscard]] static EditCommand replaceAsset(const uuids::uuid& assetUUID, AssetType type,
                                                std::string beforePath, std::string beforeClassName,
                                                std::string beforeBody, std::string afterPath,
                                                std::string afterClassName, std::string afterBody);

  [[nodiscard]] static EditCommand renameAsset(const uuids::uuid& assetUUID,
                                               std::string beforeDisplayName,
                                               std::string afterDisplayName);

  // The whole removed record (including a prefab body), so undo can re-add it exactly as addAsset would.
  [[nodiscard]] static EditCommand removeAsset(const uuids::uuid& assetUUID, AssetType type,
                                               std::string path, std::string className, std::string body);

  [[nodiscard]] CommandKind kind() const;

  [[nodiscard]] PayloadForm payloadForm() const;

  // False only for removeObject: ObjectManager::deleteObjectsMarkedForDeletion promotes the removed
  // object's children to its own parent (preserving their world placement) rather than deleting them, so
  // "undo" would have to both recreate the removed object AND reclaim those already-live children back
  // under it - restoreObject alone cannot do the second half (the children's uuids are already live in
  // the scene, so its body would collide with them), and no other op composes the two into one atomic
  // sceneEdit (EditCommand hands back exactly one payload per undo/redo). removeComponent is reversible
  // via addComponent's "data" field (see Replication.h's buildAddComponent); duplicateObject/
  // instantiatePrefab are reversible via removeSubtree (undo, by the created root's uuid alone - it
  // deletes the whole subtree immediately, unlike removeObject) and by re-running the original creating
  // op (redo - see buildRedoJSON). Every other kind targets an object/asset whose uuid is already stable,
  // so its reverse is exact.
  [[nodiscard]] bool isReversible() const;

  // Compares this command's "after" state against the live scene/registry - what undo is about to
  // revert away from. AssetRegistry may be null when the command cannot be an asset op (only
  // addAsset/renameAsset/removeAsset read it).
  [[nodiscard]] Validation validateForUndo(const ObjectManager& objectManager,
                                           const AssetRegistry* assetRegistry) const;

  // Compares this command's "before" state against the live scene/registry - what redo is about to
  // move forward from.
  [[nodiscard]] Validation validateForRedo(const ObjectManager& objectManager,
                                           const AssetRegistry* assetRegistry) const;

  // Preconditions: payloadForm() == sceneEdit, and isReversible() for the Undo direction (redo is only
  // ever attempted on a command that already passed isReversible() once, when it was undone).
  [[nodiscard]] nlohmann::json buildUndoJSON(const ObjectManager& objectManager) const;
  [[nodiscard]] nlohmann::json buildRedoJSON(const ObjectManager& objectManager) const;

  // Preconditions: payloadForm() == networkMessage, same reversibility note as above.
  [[nodiscard]] net::Message buildUndoMessage(const ObjectManager& objectManager) const;
  [[nodiscard]] net::Message buildRedoMessage(const ObjectManager& objectManager) const;

  // The uuid to report when a refusal is not about a specific field conflict (isReversible() is false,
  // or the history has nothing else to name). Validation failures carry their own conflicting uuid,
  // which is not always this one - e.g. an addObject redo can conflict on its parent instead.
  [[nodiscard]] uuids::uuid primaryUUID() const;

  // A human-readable one-line description of this command ("Rename Cube", "Edit Transform", "Delete Asset
  // Rock"), for an Edit menu that names the next undo/redo action rather than showing a bare "Undo"/"Redo"
  // label. Resolves a name from what the command itself recorded (an object's before/after name, an
  // asset's own path/className) when it can, falling back to the live scene/registry, and finally to a
  // generic kind label when neither is available. A single switch over every CommandKind with no default,
  // so a new kind is a compiler warning here instead of a silently generic label.
  [[nodiscard]] std::string describeForMenu(const ObjectManager& objectManager,
                                            const AssetRegistry* assetRegistry) const;

  [[nodiscard]] friend bool operator==(const EditCommand&, const EditCommand&) = default;

private:
  struct ComponentEditData {
    uuids::uuid objectUUID;
    std::string registryKey;
    std::string className; // empty unless a script
    std::string beforeJSON; // dumped Component::serialize() blob
    std::string afterJSON;

    friend bool operator==(const ComponentEditData&, const ComponentEditData&) = default;
  };

  struct AddObjectData {
    uuids::uuid objectUUID;
    std::optional<uuids::uuid> parentUUID;
    std::string name;
    std::size_t siblingIndex = 0; // not enforced on redo - see EditCommand.cpp

    friend bool operator==(const AddObjectData&, const AddObjectData&) = default;
  };

  struct RemoveObjectData {
    uuids::uuid objectUUID;
    std::optional<uuids::uuid> parentUUID;
    std::size_t siblingIndex = 0;
    std::string removedSubtreeJSON;

    friend bool operator==(const RemoveObjectData&, const RemoveObjectData&) = default;
  };

  struct ReparentObjectData {
    uuids::uuid objectUUID;
    std::optional<uuids::uuid> beforeParentUUID;
    std::optional<uuids::uuid> afterParentUUID;

    friend bool operator==(const ReparentObjectData&, const ReparentObjectData&) = default;
  };

  struct ReorderObjectData {
    uuids::uuid objectUUID;
    std::optional<uuids::uuid> beforeParentUUID;
    std::size_t beforeIndex = 0;
    std::optional<uuids::uuid> afterParentUUID;
    std::size_t afterIndex = 0;

    friend bool operator==(const ReorderObjectData&, const ReorderObjectData&) = default;
  };

  struct RenameObjectData {
    uuids::uuid objectUUID;
    std::string beforeName;
    std::string afterName;

    friend bool operator==(const RenameObjectData&, const RenameObjectData&) = default;
  };

  struct AddComponentData {
    uuids::uuid objectUUID;
    std::string registryKey;
    std::string className;

    friend bool operator==(const AddComponentData&, const AddComponentData&) = default;
  };

  struct RemoveComponentData {
    uuids::uuid objectUUID;
    std::string registryKey;
    std::string className;
    std::string removedComponentJSON;

    friend bool operator==(const RemoveComponentData&, const RemoveComponentData&) = default;
  };

  struct DuplicateObjectData {
    uuids::uuid sourceUUID;
    uuids::uuid duplicateUUID;
    std::optional<uuids::uuid> parentUUID;
    std::size_t siblingIndex = 0;

    friend bool operator==(const DuplicateObjectData&, const DuplicateObjectData&) = default;
  };

  struct InstantiatePrefabData {
    uuids::uuid prefabUUID;
    uuids::uuid instanceUUID;
    std::optional<uuids::uuid> parentUUID;
    std::size_t siblingIndex = 0;
    std::string prefabBodyJSON; // AssetRecord::body at record time - see the factory's comment

    friend bool operator==(const InstantiatePrefabData&, const InstantiatePrefabData&) = default;
  };

  struct AddAssetData {
    uuids::uuid assetUUID;
    AssetType type = AssetType::Unknown;
    std::string path;      // file path (model/texture/script) or display name (prefab/scene)
    std::string className; // scripts only
    std::string body;      // prefabs only

    friend bool operator==(const AddAssetData&, const AddAssetData&) = default;
  };

  struct ReplaceAssetData {
    uuids::uuid assetUUID;
    AssetType type = AssetType::Unknown;
    std::string beforePath;
    std::string beforeClassName;
    std::string beforeBody;
    std::string afterPath;
    std::string afterClassName;
    std::string afterBody;

    friend bool operator==(const ReplaceAssetData&, const ReplaceAssetData&) = default;
  };

  struct RenameAssetData {
    uuids::uuid assetUUID;
    std::string beforeDisplayName;
    std::string afterDisplayName;

    friend bool operator==(const RenameAssetData&, const RenameAssetData&) = default;
  };

  struct RemoveAssetData {
    uuids::uuid assetUUID;
    AssetType type = AssetType::Unknown;
    std::string path;
    std::string className;
    std::string body;

    friend bool operator==(const RemoveAssetData&, const RemoveAssetData&) = default;
  };

  using CommandData = std::variant<ComponentEditData, AddObjectData, RemoveObjectData, ReparentObjectData,
                                   ReorderObjectData, RenameObjectData, AddComponentData,
                                   RemoveComponentData, DuplicateObjectData, InstantiatePrefabData,
                                   AddAssetData, ReplaceAssetData, RenameAssetData, RemoveAssetData>;

  CommandKind m_kind = CommandKind::componentEdit;
  CommandData m_data;
};

}

#endif //EDITCOMMAND_H
