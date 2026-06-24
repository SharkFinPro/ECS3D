#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <vector>

namespace net {

enum class MessageType : uint8_t {
  snapshot,
  stateDelta,
  inputState
  // TODO: add handshake/auth (carries the role: Role::Editor vs Role::Player), edit commands
  // TODO:   (editor only), and scene lifecycle events (start/stop/reset).
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
