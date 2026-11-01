#ifndef MANAGEDCLIENTTRANSPORT_H
#define MANAGEDCLIENTTRANSPORT_H

#include "ClientTransport.h"
#include <memory>

class ManagedHost;

namespace net {

// The ClientTransport backed by the C# ECS3DNetTransport assembly, reached through ManagedHost delegates.
class ManagedClientTransport : public ClientTransport {
public:
  explicit ManagedClientTransport(std::shared_ptr<ManagedHost> host);

  bool connect(const std::string& host, int32_t port, Role role, const std::string& authToken,
               const ClientCallbacks& callbacks) override;

  void disconnect() override;

  void send(uint8_t type, std::span<const uint8_t> payload) const override;

private:
  std::shared_ptr<ManagedHost> m_host;

  // Resolved [UnmanagedCallersOnly] entrypoints in the C# transport assembly.
  void* m_connectFn = nullptr;
  void* m_disconnectFn = nullptr;
  void* m_sendFn = nullptr;
};

}

#endif //MANAGEDCLIENTTRANSPORT_H
