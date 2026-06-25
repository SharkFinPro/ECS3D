#ifndef REPLICATION_H
#define REPLICATION_H

#include <nlohmann/json_fwd.hpp>
#include <memory>
#include <uuid.h>

class ObjectManager;
class Component;

// Per-tick state replication. The full Snapshot (on join) is just ProjectSerializer::serialize(); the
// StateDelta is the lighter per-tick stream: each object's uuid + transform. The server builds it from
// its authoritative scene, the client applies it to its replicated view. This lives in ECS3DData (it
// reads/writes the scene data); the net layer only carries the resulting bytes.
namespace replication {

[[nodiscard]] nlohmann::json buildStateDelta(const ObjectManager& objectManager);

void applyStateDelta(const ObjectManager& objectManager, const nlohmann::json& delta);

// The editor's return path: a single component edit, carried as { object, type, [className], data },
// where data is the component's own serialize() blob. The server applies it to its authoritative scene
// (reusing each component's loadFromJSON) and re-broadcasts so every view converges. Reuses the
// existing serialize()/loadFromJSON boundary — the net layer never names a component type.
[[nodiscard]] nlohmann::json buildComponentEdit(const uuids::uuid& objectUUID,
                                                const std::shared_ptr<Component>& component);

void applyComponentEdit(const ObjectManager& objectManager, const nlohmann::json& edit);

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

void applySceneEdit(ObjectManager& objectManager, const nlohmann::json& edit);

}



#endif //REPLICATION_H
