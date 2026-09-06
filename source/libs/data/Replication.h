#ifndef REPLICATION_H
#define REPLICATION_H

#include <nlohmann/json_fwd.hpp>
#include <memory>
#include <uuid.h>

class Object;
class ObjectManager;
class Component;
class AssetRegistry;
class SceneManager;
class ComponentRegistry;

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

// Structural edits (add/remove object or component). Unlike a value edit these change the scene graph,
// so the server applies them and re-broadcasts a full Snapshot rather than replicating per-op - the
// client/editor just rebuild from the snapshot. Each is carried as { op, ... }.
[[nodiscard]] nlohmann::json buildAddObject(const std::string& name,
                                            const uuids::uuid* parentUUID = nullptr);

[[nodiscard]] nlohmann::json buildRemoveObject(const uuids::uuid& objectUUID);

[[nodiscard]] nlohmann::json buildAddComponent(const uuids::uuid& objectUUID,
                                               const std::string& componentKey);

[[nodiscard]] nlohmann::json buildRemoveComponent(const uuids::uuid& objectUUID,
                                                  const std::shared_ptr<Component>& component);

[[nodiscard]] nlohmann::json buildDuplicateObject(const uuids::uuid& objectUUID);

[[nodiscard]] nlohmann::json buildReparentObject(const uuids::uuid& objectUUID,
                                                 const uuids::uuid* parentUUID = nullptr);

[[nodiscard]] nlohmann::json buildRenameObject(const uuids::uuid& objectUUID,
                                               const std::string& name);

[[nodiscard]] nlohmann::json buildAddScript(const uuids::uuid& objectUUID,
                                            const std::string& className);

// Instantiate a prefab asset into the scene at the transform stored in its body. Unlike every other op
// this one names an asset rather than an existing object, so applySceneEdit needs the AssetRegistry to
// resolve the prefab's uuid to its body - pass it whenever prefab ops are possible (the authoritative
// server always does).
[[nodiscard]] nlohmann::json buildInstantiatePrefab(const uuids::uuid& prefabUUID);

// Why a structural edit did not take. Same reasoning as ComponentEditResult: the authority has to tell a
// payload it could not parse apart from an op it understood and refused, because only the first says the
// sender and the authority disagree about the wire, and only the second is a normal thing for an editor
// to send. There is no partially-applied case: an op either changes the graph or it does not, and the
// one that builds as it goes (instantiatePrefab) unwinds its own subtree before it throws.
//
// Not [[nodiscard]], for the same reason: an editor applying an edit to its own scratch scene has
// nothing to do with the answer. The authority does.
enum class SceneEditResult {
  applied,
  malformedEdit,     // no op, an op nothing handles, or a field the op needs missing or unparseable
  unknownObject,     // names an object this scene does not have
  unknownComponent,  // names a component type that does not exist, or one the object is not carrying
  unknownAsset,      // instantiatePrefab named an asset with no usable body
  rejected,          // well formed and refused: a reparent that would cycle, or that changes nothing
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

}



#endif //REPLICATION_H
