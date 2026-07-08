#ifndef NETSERVER_H
#define NETSERVER_H

#include "MessageQueue.h"
#include <cstdint>
#include <memory>
#include <string>

class ManagedHost;

namespace net {

class NetServer {
public:
  explicit NetServer(std::shared_ptr<ManagedHost> host);

  // editMode enables granting Role::editor at the handshake; authToken (if non-empty) is the token an
  // editor must then present to be authorized.
  void start(int port, bool editMode, const std::string& authToken);

  void stop();

  void broadcast(const Message& message) const;

  // The number of clients currently connected to the transport. Used by an ephemeral (editor/client-
  // spawned) server to detect when its last connection drops; returns 0 before start().
  [[nodiscard]] int connectionCount() const;

  // senderId is set to the stable connection id of the message's origin, so the app can route input to
  // the right per-connection slot.
  [[nodiscard]] bool poll(Message& message, int32_t& senderId);

  // Called from the C# socket thread (via the registered native callback) to hand an inbound message
  // to the tick thread; the inbox is mutex-protected so the threads never collide. connId is the stable
  // per-connection id the transport assigns each client.
  void enqueue(int32_t connId, uint8_t type, const uint8_t* data, int32_t len);

private:
  std::shared_ptr<ManagedHost> m_host;

  MessageQueue m_inbox;

  bool m_editMode = false;
  bool m_started = false;

  // Resolved [UnmanagedCallersOnly] entrypoints in the C# transport assembly.
  void* m_startFn = nullptr;
  void* m_stopFn = nullptr;
  void* m_broadcastFn = nullptr;
  void* m_connectionCountFn = nullptr;
  void* m_setCallbackFn = nullptr;
};

}



#endif //NETSERVER_H
