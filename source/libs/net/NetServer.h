#ifndef NETSERVER_H
#define NETSERVER_H

#include "MessageQueue.h"
#include <cstdint>
#include <memory>

class ManagedHost;

namespace net {

class NetServer {
public:
  explicit NetServer(std::shared_ptr<ManagedHost> host);

  void start(int port, bool editMode);

  void stop();

  void broadcast(const Message& message);

  [[nodiscard]] bool poll(Message& message);

  // Called from the C# socket thread (via the registered native callback) to hand an inbound message
  // to the tick thread; the inbox is mutex-protected so the threads never collide.
  void enqueue(uint8_t type, const uint8_t* data, int32_t len);

private:
  std::shared_ptr<ManagedHost> m_host;

  MessageQueue m_inbox;

  bool m_editMode = false;
  bool m_started = false;

  // Resolved [UnmanagedCallersOnly] entrypoints in the C# transport assembly.
  void* m_startFn = nullptr;
  void* m_stopFn = nullptr;
  void* m_broadcastFn = nullptr;
  void* m_setCallbackFn = nullptr;
};

}



#endif //NETSERVER_H
