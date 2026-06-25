#ifndef REPLICATION_H
#define REPLICATION_H

#include <nlohmann/json_fwd.hpp>

class ObjectManager;

// Per-tick state replication. The full Snapshot (on join) is just ProjectSerializer::serialize(); the
// StateDelta is the lighter per-tick stream: each object's uuid + transform. The server builds it from
// its authoritative scene, the client applies it to its replicated view. This lives in ECS3DData (it
// reads/writes the scene data); the net layer only carries the resulting bytes.
namespace replication {

[[nodiscard]] nlohmann::json buildStateDelta(const ObjectManager& objectManager);

void applyStateDelta(const ObjectManager& objectManager, const nlohmann::json& delta);

}



#endif //REPLICATION_H
