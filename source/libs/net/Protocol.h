#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <vector>

namespace net {

// The default port the server listens on and the client/editor connect to (singleplayer, local-MP, and
// remote all share the same wire). Apps use this unless overridden, so they find each other out of the
// box instead of defaulting to port 0.
inline constexpr int defaultPort = 3000;

enum class MessageType : uint8_t {
  join,         // client -> server: request the initial Snapshot (carries role + auth at handshake)
  snapshot,     // server -> client: full project/scene state (ProjectSerializer::serialize())
  stateDelta,   // server -> client: per-tick transform stream (replication::buildStateDelta)
  inputState,    // client -> server: local input ({ keys, focused }) for the scripts to read
  editComponent, // editor -> server -> all: a single component value edit (replication::buildComponentEdit)
  sceneEdit,     // editor -> server: a structural edit (add/remove object/component); server re-snapshots
  sceneControl   // editor -> server: scene lifecycle ({ op: start|pause|stop }); server re-snapshots
  // TODO: add the auth payload on join (Role::editor needs the server's edit gate + a token).
  // editComponent/sceneEdit/sceneControl are the editor's mutation path; inputState is the play path.
};

enum class Role : uint8_t {
  player,
  editor
};

struct Message {
  MessageType type;

  // Opaque payload: the existing serialize() JSON as bytes. The net layer never references a
  // specific component type, only this generic blob.
  std::vector<uint8_t> payload;
};

}



#endif //PROTOCOL_H
