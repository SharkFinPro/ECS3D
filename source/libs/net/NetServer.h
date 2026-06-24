#ifndef NETSERVER_H
#define NETSERVER_H

#include "MessageQueue.h"
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

private:
  std::shared_ptr<ManagedHost> m_host;

  MessageQueue m_inbox;
  MessageQueue m_outbox;

  bool m_editMode = false;
};

}



#endif //NETSERVER_H
