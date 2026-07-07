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
// existing serialize()/loadFromJSON boundary — the net layer never names a component type.
[[nodiscard]] net::Message buildComponentEdit(const uuids::uuid& objectUUID,
                                              const std::shared_ptr<Component>& component);

void applyComponentEdit(const ObjectManager& objectManager, const net::Message& edit);

// Structural edits (add/remove object or component). Unlike a value edit these change the scene graph,
// so the server applies them and re-broadcasts a full Snapshot rather than replicating per-op — the
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

void applySceneEdit(ObjectManager& objectManager, const nlohmann::json& edit);

// Runtime spawn/destroy replication. Unlike the editor's structural edits (which re-snapshot), a script
// spawning or destroying an object at runtime replicates incrementally: the server broadcasts one packed
// object (spawn) or a uuid (destroy), and each view splices it into / out of its replicated scene. Keeps
// frequent runtime spawning off the full-snapshot path.
[[nodiscard]] net::Message buildObjectSpawned(const Object& object);

[[nodiscard]] net::Message buildObjectDestroyed(const uuids::uuid& objectUUID);

void applyObjectSpawned(ObjectManager& objectManager, const net::Message& message);

void applyObjectDestroyed(ObjectManager& objectManager, const net::Message& message);

// Register an imported/created asset ({ assetType, uuid, path|name, [className] }). Shared by the
// server (authoritative) and the editor (instant local feedback). Models/textures/scripts go into the
// AssetRegistry; a scene also gets an empty SceneAsset in the SceneManager.
void applyAddAsset(AssetRegistry& assetRegistry,
                   SceneManager& sceneManager,
                   const std::shared_ptr<ComponentRegistry>& componentRegistry,
                   const nlohmann::json& asset);

// Wire (de)serialization for an addAsset blob. The schema is open-ended per asset type, but only a fixed
// set of string fields is ever consumed (see applyAddAsset), so each is packed length-prefixed (empty
// when absent) instead of as JSON. The pairing unpack rebuilds the blob applyAddAsset expects.
[[nodiscard]] net::Message packAddAsset(const nlohmann::json& asset);

[[nodiscard]] nlohmann::json unpackAddAsset(const net::Message& message);

}



#endif //REPLICATION_H
