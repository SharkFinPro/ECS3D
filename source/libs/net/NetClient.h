#ifndef NETCLIENT_H
#define NETCLIENT_H

#include "ClientConnectionState.h"
#include "ClientTransport.h"
#include "MessageQueue.h"
#include <cstdint>
#include <memory>
#include <string>

namespace net {

class NetClient {
public:
  explicit NetClient(const std::shared_ptr<ClientTransport>& transport);

  ~NetClient();

  void connect(const std::string& host, int port, Role role, const std::string& authToken);

  void disconnect();

  void send(const Message& message) const;

  [[nodiscard]] bool isConnected() const { return m_state.isConnected(); }

  [[nodiscard]] bool poll(Message& message);

  // Called from the C# socket thread (via the registered native callback) to hand an inbound message
  // (snapshot on join, state delta per tick) to the render thread; the inbox is mutex-protected.
  void enqueue(uint8_t type, const uint8_t* data, int32_t len);

  // Called from the C# socket thread (via the registered native callback) when the client's receive
  // loop exits. No-ops if disconnect() requested it - only an unrequested drop is a lost connection.
  void enqueueDisconnect();

  // Returns whether the connection was lost since the last call, clearing the flag - so an app that
  // polls once a frame sees the event exactly once.
  [[nodiscard]] bool takeConnectionLost();

private:
  std::shared_ptr<ClientTransport> m_transport;

  MessageQueue m_inbox;

  ClientConnectionState m_state;
};

}



#endif //NETCLIENT_H
