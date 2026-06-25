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

}



#endif //REPLICATION_H
