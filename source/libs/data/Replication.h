#ifndef REPLICATION_H
#define REPLICATION_H

#include <nlohmann/json_fwd.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <uuid.h>
#include <vector>

class Object;
class ObjectManager;
class Component;
class AssetRegistry;
class SceneManager;
class ComponentRegistry;

enum class LogCategory;

namespace net {
  class Message;
}

// Per-tick state replication. The full Snapshot (on join) is the packed project blob (ProjectPacker);
// the StateDelta is the lighter per-tick stream: each object's uuid + local transform, packed as binary
// straight into the message (count-prefixed entries) rather than JSON. The server packs it from its
// authoritative scene, the client unpacks it into its replicated view. This lives in ECS3DData (it
// reads/writes the scene data); the net layer only carries the resulting bytes.
namespace replication {

void packStateDelta(net::Message& message, const ObjectManager& objectManager);

void unpackStateDelta(const ObjectManager& objectManager, const net::Message& message);

// The editor's return path: a single component edit, carried as { object, type, [className], data },
// where data is the component's own serialize() blob. The server applies it to its authoritative scene
// (reusing each component's loadFromJSON) and re-broadcasts so every view converges. Reuses the
// existing serialize()/loadFromJSON boundary - the net layer never names a component type.
[[nodiscard]] net::Message buildComponentEdit(const uuids::uuid& objectUUID,
                                              const std::shared_ptr<Component>& component);

// How an edit ended. The failures mean different things: a payload that will not parse is always a bug -
// corruption, a truncated read, a protocol mismatch - while a missing target is routine, since the server
// rebroadcasts every edit and a view can receive one for an object it has not been sent yet or has
// already dropped. Collapsing them into one silent return hid the serious case behind the benign one.
//
// partiallyApplied is the ugly middle: a component unpacks field by field as it reads, so a payload that
// runs out mid-body leaves it half written. The authority recovers by re-snapshotting.
//
// Not [[nodiscard]]: a replicated view legitimately ignores every failure here. The authority does not.
enum class ComponentEditResult {
  applied,
  malformedPayload,
  partiallyApplied,
  unknownObject,
  unknownComponent
};

ComponentEditResult applyComponentEdit(const ObjectManager& objectManager, const net::Message& edit);

// Human-readable reason a component edit was not applied. Shared by every app (server, client, editor)
// so the wording stays one table instead of three.
[[nodiscard]] std::string_view describe(ComponentEditResult result);

// Logs a component edit a replicated view could not apply. A missing object or component is the
// ordinary case for a view that is a round trip behind the authority, so it goes out at debug; a
// payload that did not parse or ran out mid-component is a real divergence and goes out at error.
// No-op for applied.
void logMissedComponentEdit(ComponentEditResult result, const net::Message& edit, LogCategory category);

// Structural edits (add/remove object or component). Unlike a value edit these change the scene graph,
// so the server applies them and re-broadcasts a full Snapshot rather than replicating per-op - the
// client/editor just rebuild from the snapshot. Each is carried as { op, ... }.
//
// The three ops that create an object (addObject, duplicateObject, instantiatePrefab) take an optional
// uuid for the object they create, carried as "uuid". Without it the authority picks one and the sender
// never learns which object its edit produced, which is what the undo history needs to record the edit
// (see edits/EditCommand.h). A uuid that does not parse is a malformed edit; the nil uuid, or one the
// scene already holds, is refused rather than merged into or quietly replaced.
[[nodiscard]] nlohmann::json buildAddObject(const std::string& name,
                                            const uuids::uuid* parentUUID = nullptr,
                                            const uuids::uuid* objectUUID = nullptr);

[[nodiscard]] nlohmann::json buildRemoveObject(const uuids::uuid& objectUUID);

// data, when given, is the component's own serialize() blob (loadFromJSON'd onto the freshly created
// component before it is attached) - what undo of a removeComponent needs to put the exact removed
// component back in one op, rather than a blank default the way a fresh addComponent normally creates.
[[nodiscard]] nlohmann::json buildAddComponent(const uuids::uuid& objectUUID,
                                               const std::string& componentKey,
                                               const nlohmann::json* data = nullptr);

[[nodiscard]] nlohmann::json buildRemoveComponent(const uuids::uuid& objectUUID,
                                                  const std::shared_ptr<Component>& component);

[[nodiscard]] nlohmann::json buildDuplicateObject(const uuids::uuid& objectUUID,
                                                  const uuids::uuid* duplicateUUID = nullptr);

[[nodiscard]] nlohmann::json buildReparentObject(const uuids::uuid& objectUUID,
                                                 const uuids::uuid* parentUUID = nullptr);

// Place objectUUID at sibling index within parent's child list (absent parent = scene root), whether
// that parent is the one it already has (a plain reorder) or a different one (reparentObject only ever
// appends - this is the drop-BETWEEN-siblings counterpart). index is read against the target list AFTER
// objectUUID is removed from wherever it sits now; applySceneEdit refuses rather than clamps an index
// past that list's end, and preserves world placement across a parent change the same way reparentObject
// does.
[[nodiscard]] nlohmann::json buildReorderObject(const uuids::uuid& objectUUID,
                                                const uuids::uuid* parentUUID,
                                                std::size_t index);

[[nodiscard]] nlohmann::json buildRenameObject(const uuids::uuid& objectUUID,
                                               const std::string& name);

[[nodiscard]] nlohmann::json buildAddScript(const uuids::uuid& objectUUID,
                                            const std::string& className);

// Instantiate a prefab asset into the scene at the transform stored in its body. Unlike every other op
// this one names an asset rather than an existing object, so applySceneEdit needs the AssetRegistry to
// resolve the prefab's uuid to its body - pass it whenever prefab ops are possible (the authoritative
// server always does). An absent parentUUID instantiates at the scene root (the original behavior);
// passing one instantiates as a child of that object instead, e.g. dropping the prefab onto it in the tree.
[[nodiscard]] nlohmann::json buildInstantiatePrefab(const uuids::uuid& prefabUUID,
                                                    const uuids::uuid* parentUUID = nullptr,
                                                    const uuids::uuid* instanceUUID = nullptr);

// Rebuild a subtree from an inline serialized body (not an asset uuid, unlike instantiatePrefab) under
// parent (absent = scene root) at sibling index. Undo of a deletion: unlike every other creating op this
// one carries the body's own uuids, which applySceneEdit preserves rather than reassigning - see
// ObjectManager::restoreSubtree for why.
//
// adopt, when given, is the { "object", "index", "transform" } list for undoing a removeObject
// specifically: deleteObjectsMarkedForDeletion promotes the removed object's direct children up to its
// own parent rather than deleting them, so undoing the removal has to both recreate the removed object
// AND reclaim those still-live children back under it, in the same atomic op - restoreObject's own body
// cannot carry them (their uuids are already live, which the body-walk collision check would refuse). Each
// entry names a still-live child by uuid, the sibling index it held under the removed object, and that
// object's own recorded local Transform blob (the exact pre-removal values - deleteObjectsMarkedForDeletion
// rewrote the live ones via WorldPlacement math when it promoted the child). See applySceneEdit's
// restoreObject handling for the validation and apply order.
[[nodiscard]] nlohmann::json buildRestoreObject(const nlohmann::json& body,
                                                const uuids::uuid* parentUUID,
                                                std::size_t index,
                                                const nlohmann::json* adopt = nullptr);

// Delete objectUUID and its whole subtree immediately, unlike removeObject (which promotes the removed
// object's own children to its parent instead) - see ObjectManager::removeSubtree.
[[nodiscard]] nlohmann::json buildRemoveSubtree(const uuids::uuid& objectUUID);

// Several ops applied as one atomic edit: applySceneEdit dry-runs every op in ops, in order, against a
// scratch copy of the scene first, and only applies them for real if every one of them would have. Used
// to make a multi-object action (deleting or duplicating a whole editor selection) one sceneEdit, one
// snapshot, and one undo/redo entry. Not nestable - an op in ops that is itself "batch" is malformedEdit.
[[nodiscard]] nlohmann::json buildBatch(const std::vector<nlohmann::json>& ops);

// Why a structural edit did not take. Same reasoning as ComponentEditResult: the authority has to tell a
// payload it could not parse apart from an op it understood and refused, because only the first says the
// sender and the authority disagree about the wire, and only the second is a normal thing for an editor
// to send.
//
// There is no partially-applied result, because in practice an op either changes the graph or it does
// not: the one that builds as it goes (instantiatePrefab) unwinds its own subtree before it throws. The
// exception is a reparent, which detaches before it reattaches - an allocation failure between the two
// would strand the object. failed covers what is reachable; a bad_alloc there is rethrown.
//
// Not [[nodiscard]], for the same reason: an editor applying an edit to its own scratch scene has
// nothing to do with the answer. The authority does.
enum class SceneEditResult {
  applied,
  malformedEdit,     // no op, an op nothing handles, or a field the op needs missing or unparseable
  unknownObject,     // names an object this scene does not have
  unknownComponent,  // names a component type that does not exist, or one the object is not carrying
  unknownAsset,      // instantiatePrefab named an asset with no usable body
  rejected,          // well formed and refused: a reparent or reorder that would cycle or that changes
                     // nothing, a reorder whose index is out of range for its target list, a restoreObject
                     // whose body names a uuid already live or would exceed maxObjectDepth, a restoreObject
                     // whose adopt list names a uuid twice (or one the body also names) or a uuid living
                     // under a different parent than the one it is being restored under, or a creating
                     // op naming the nil uuid or one the scene is already using
  failed             // threw part way through, e.g. a prefab body naming a component this build lacks
};

SceneEditResult applySceneEdit(ObjectManager& objectManager, const nlohmann::json& edit,
                               const AssetRegistry* assetRegistry = nullptr);

// Runtime spawn/destroy replication. Unlike the editor's structural edits (which re-snapshot), a script
// spawning or destroying an object at runtime replicates incrementally: the server broadcasts one packed
// object (spawn) or a uuid (destroy), and each view splices it into / out of its replicated scene. Keeps
// frequent runtime spawning off the full-snapshot path.
[[nodiscard]] net::Message buildObjectSpawned(const Object& object);

[[nodiscard]] net::Message buildObjectDestroyed(const uuids::uuid& objectUUID);

void applyObjectSpawned(ObjectManager& objectManager, const net::Message& message);

void applyObjectDestroyed(ObjectManager& objectManager, const net::Message& message);

// A script added/removed a component on an already-live object (ComponentOpsBindings): unlike spawn/
// destroy the object itself isn't new or gone, so its whole current subtree is re-packed (Object::pack
// recurses through children) and the receiver unpacks it into the existing object it already has by uuid -
// Object::unpack reconciles both a component set and children against what was packed, at every level of
// the subtree, so an add, a remove, or several of either in one tick all converge to the same state a
// fresh unpack of that subtree would produce. Still far narrower than the whole-project snapshot this
// replaces the editor's sceneEdit addComponent/removeComponent ops would otherwise need for the same
// change. Ignored (no-op) if the uuid names no object the receiver currently has - routine for a view that
// has not been sent it yet or has already dropped it, the same as an objectDestroyed for an object never
// spliced in.
[[nodiscard]] net::Message buildObjectComponentsChanged(const Object& object);

void applyObjectComponentsChanged(ObjectManager& objectManager, const net::Message& message);

// Register an imported/created asset ({ assetType, uuid, path|name, [className], [body] }). Shared by the
// server (authoritative) and the editor (instant local feedback). Models/textures/scripts/prefabs go
// into the AssetRegistry; a scene also gets an empty SceneAsset in the SceneManager. A prefab carries its
// serialized-object `body` inline (there is no file on disk).
void applyAddAsset(AssetRegistry& assetRegistry,
                   SceneManager& sceneManager,
                   const std::shared_ptr<ComponentRegistry>& componentRegistry,
                   const nlohmann::json& asset);

// Wire (de)serialization for an addAsset blob. The schema is open-ended per asset type, but only a fixed
// set of string fields is ever consumed (see applyAddAsset), so each is packed length-prefixed (empty
// when absent) instead of as JSON. The pairing unpack rebuilds the blob applyAddAsset expects.
[[nodiscard]] net::Message packAddAsset(const nlohmann::json& asset);

[[nodiscard]] nlohmann::json unpackAddAsset(const net::Message& message);

// Asset mutation ({ uuid, [displayName] }). Rename is a display-name override only; remove drops the
// record and lets references dangle. Both mirror addAsset's local-apply-then-send shape:
// the editor applies locally for instant feedback and sends the op; the server applies it authoritatively
// and re-snapshots. The build* helpers assemble the blob; pack*/unpack* carry it on the wire.
[[nodiscard]] nlohmann::json buildRenameAsset(const uuids::uuid& assetUUID, const std::string& displayName);

[[nodiscard]] nlohmann::json buildRemoveAsset(const uuids::uuid& assetUUID);

void applyRenameAsset(AssetRegistry& assetRegistry, const nlohmann::json& op);

void applyRemoveAsset(AssetRegistry& assetRegistry, const nlohmann::json& op);

[[nodiscard]] net::Message packRenameAsset(const nlohmann::json& op);

[[nodiscard]] nlohmann::json unpackRenameAsset(const net::Message& message);

[[nodiscard]] net::Message packRemoveAsset(const nlohmann::json& op);

[[nodiscard]] nlohmann::json unpackRemoveAsset(const net::Message& message);

// The client/editor's per-frame local input, sent so the headless server (no GLFW window of its own) can
// feed the scripts through InputState. focused/keysPressed/mouse are captured client-side; hasMouse is
// false only when the parsed payload predates the mouse block (see parseInputState).
struct InputStatePayload {
  bool focused = false;
  std::vector<int> keysPressed;

  bool hasMouse = false;
  float mouseX = 0.0f;
  float mouseY = 0.0f;
  float mouseDeltaX = 0.0f;
  float mouseDeltaY = 0.0f;
  float scrollY = 0.0f;
  uint8_t buttons = 0;
};

[[nodiscard]] net::Message buildInputState(bool focused, const std::vector<int>& keysPressed,
                                           float mouseX, float mouseY, float mouseDeltaX, float mouseDeltaY,
                                           float scrollY, uint8_t buttons);

// nullopt only for a key count that claims more keys than the payload has bytes left to hold - every
// other malformed shape (a truncated bool/count, or a reader run off the end) throws std::runtime_error
// the same way every other wire read here does. The mouse block is optional on the wire (an older client
// may predate it): fewer bytes than the block needs leaves hasMouse false rather than throwing, so those
// bytes (if any) are simply left unread.
[[nodiscard]] std::optional<InputStatePayload> parseInputState(const net::Message& message);

}



#endif //REPLICATION_H
