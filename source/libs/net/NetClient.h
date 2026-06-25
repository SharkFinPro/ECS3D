#ifndef NETCLIENT_H
#define NETCLIENT_H

#include "MessageQueue.h"
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

  void send(const Message& message);

  [[nodiscard]] bool poll(Message& message);

  // Called from the C# socket thread (via the registered native callback) to hand an inbound message
  // (snapshot on join, state delta per tick) to the render thread; the inbox is mutex-protected.
  void enqueue(uint8_t type, const uint8_t* data, int32_t len);

private:
  std::shared_ptr<ManagedHost> m_host;

  MessageQueue m_inbox;

  bool m_connected = false;

  // Resolved [UnmanagedCallersOnly] entrypoints in the C# transport assembly.
  void* m_connectFn = nullptr;
  void* m_disconnectFn = nullptr;
  void* m_sendFn = nullptr;
  void* m_setCallbackFn = nullptr;
};

}



#endif //NETCLIENT_H
