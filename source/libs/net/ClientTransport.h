#ifndef CLIENTTRANSPORT_H
#define CLIENTTRANSPORT_H

#include <Protocol.h>
#include <cstdint>
#include <span>
#include <string>

namespace net {

struct ClientCallbacks
{
  void (*receive)(uint8_t type, const uint8_t* data, int32_t len) = nullptr;
  void (*disconnect)() = nullptr;
};

class ClientTransport {
public:
  virtual ~ClientTransport() = default;

  // Registers the callbacks, then connects. Returns whether the connection was established.
  virtual bool connect(const std::string& host, int32_t port, Role role, const std::string& authToken,
                       const ClientCallbacks& callbacks) = 0;

  virtual void disconnect() = 0;

  virtual void send(uint8_t type, std::span<const uint8_t> payload) const = 0;
};

}

#endif //CLIENTTRANSPORT_H
