#ifndef NETCLIENT_H
#define NETCLIENT_H

#include "MessageQueue.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

class ManagedHost;

namespace net {

class NetClient {
public:
  explicit NetClient(std::shared_ptr<ManagedHost> host);

  void connect(const std::string& host, int port, Role role, const std::string& authToken);

  void disconnect();

  void send(const Message& message) const;

  [[nodiscard]] bool isConnected() const { return m_connected; }

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
  std::shared_ptr<ManagedHost> m_host;

  MessageQueue m_inbox;

  std::atomic<bool> m_connected = false;
  std::atomic<bool> m_connectionLost = false;

  // Set before calling the managed disconnect, so enqueueDisconnect (racing in on the socket thread as
  // that same disconnect closes the socket) knows the drop was requested, not a lost connection.
  std::atomic<bool> m_disconnectRequested = false;

  // Resolved [UnmanagedCallersOnly] entrypoints in the C# transport assembly.
  void* m_connectFn = nullptr;
  void* m_disconnectFn = nullptr;
  void* m_sendFn = nullptr;
  void* m_setCallbackFn = nullptr;
  void* m_setDisconnectCallbackFn = nullptr;
  void* m_setLogCallbackFn = nullptr;
};

}



#endif //NETCLIENT_H
