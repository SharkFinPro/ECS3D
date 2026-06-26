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
  sceneControl,  // editor -> server: scene lifecycle ({ op: start|pause|stop|loadScene }); server re-snapshots
  loadProject,   // editor -> server: replace the project with this serialized blob; server re-snapshots
  addAsset,      // editor -> server: register a new asset (model/texture/script/scene); server re-snapshots
  editStatus     // server -> client: whether this server accepts edits ({ editable: bool }); sent on join
  // editComponent/sceneEdit/sceneControl/loadProject/addAsset are the editor's mutation path; the server
  // only honors them from a connection it authorized as Role::editor at the transport handshake (which
  // carries role + token out of band, ahead of any message here), and only on an edit-mode server. An
  // editor connecting to a non-edit server is admitted read-only (it views but cannot mutate).
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
