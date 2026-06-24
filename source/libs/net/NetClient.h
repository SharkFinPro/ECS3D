#ifndef NETCLIENT_H
#define NETCLIENT_H

#include "MessageQueue.h"
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

private:
  std::shared_ptr<ManagedHost> m_host;

  MessageQueue m_inbox;
  MessageQueue m_outbox;
};

}



#endif //NETCLIENT_H
