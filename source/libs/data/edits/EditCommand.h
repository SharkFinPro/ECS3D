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
  renameObject,
  addComponent,
  removeComponent,
  duplicateObject,
  instantiatePrefab,
  addAsset,
  renameAsset,
  removeAsset
};

// Whether a command's undo/redo payload is a structural sceneEdit op (nlohmann::json, built by
// Replication.h's build* functions and sent as a sceneEdit message by the caller) or an already
// wire-ready net::Message (editComponent/addAsset/renameAsset/removeAsset, built by Replication.h's
// pack* functions). Fixed per kind, so a caller branches on this rather than guessing which accessor to
// call.
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

  [[nodiscard]] static EditCommand renameObject(const uuids::uuid& objectUUID,
                                                std::string beforeName,
                                                std::string afterName);

  [[nodiscard]] static EditCommand addComponent(const uuids::uuid& objectUUID,
                                                std::string registryKey,
                                                std::string className = {});

  // Not reversible (see isReversible). removedComponent is the removed component's serialize() blob;
  // registryKey/className are derived from it the same way componentEdit derives them.
  [[nodiscard]] static EditCommand removeComponent(const uuids::uuid& objectUUID,
                                                   const nlohmann::json& removedComponent);

  // Not reversible (see isReversible).
  [[nodiscard]] static EditCommand duplicateObject(const uuids::uuid& sourceUUID,
                                                   const uuids::uuid& duplicateUUID,
                                                   const std::optional<uuids::uuid>& parentUUID,
                                                   std::size_t siblingIndex);

  // Not reversible (see isReversible).
  [[nodiscard]] static EditCommand instantiatePrefab(const uuids::uuid& prefabUUID,
                                                     const uuids::uuid& instanceUUID,
                                                     const std::optional<uuids::uuid>& parentUUID,
                                                     std::size_t siblingIndex);

  [[nodiscard]] static EditCommand addAsset(const uuids::uuid& assetUUID, AssetType type,
                                            std::string path, std::string className, std::string body);

  [[nodiscard]] static EditCommand renameAsset(const uuids::uuid& assetUUID,
                                               std::string beforeDisplayName,
                                               std::string afterDisplayName);

  // The whole removed record (including a prefab body), so undo can re-add it exactly as addAsset would.
  [[nodiscard]] static EditCommand removeAsset(const uuids::uuid& assetUUID, AssetType type,
                                               std::string path, std::string className, std::string body);

  [[nodiscard]] CommandKind kind() const;

  [[nodiscard]] PayloadForm payloadForm() const;

  // False for a kind whose reverse cannot be built without either losing data or corrupting the scene:
  // - removeObject/removeComponent would have to recreate something WITH its prior field values in a
  //   single wire op, and addComponent/addObject only ever create blank defaults - there is no one-shot
  //   "add with this data" op to build a faithful reverse from.
  // - duplicateObject/instantiatePrefab create a whole subtree (fresh uuids throughout); undoing by
  //   removing just the created root would not remove the subtree, because ObjectManager::removeObject
  //   reparents children up to the removed object's parent rather than deleting them (see
  //   ObjectManager::deleteObjectsMarkedForDeletion) - so the created children would be stranded in the
  //   scene instead of gone.
  // Every other kind targets an object/asset whose uuid is already stable, so its reverse is exact.
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
                                   RenameObjectData, AddComponentData, RemoveComponentData,
                                   DuplicateObjectData, InstantiatePrefabData, AddAssetData,
                                   RenameAssetData, RemoveAssetData>;

  CommandKind m_kind = CommandKind::componentEdit;
  CommandData m_data;
};

}

#endif //EDITCOMMAND_H
