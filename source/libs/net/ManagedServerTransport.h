#ifndef MANAGEDSERVERTRANSPORT_H
#define MANAGEDSERVERTRANSPORT_H

#include "ServerTransport.h"
#include <memory>

class ManagedHost;

namespace net {

// The ServerTransport backed by the C# ECS3DNetTransport assembly, reached through ManagedHost delegates.
class ManagedServerTransport : public ServerTransport {
public:
  explicit ManagedServerTransport(std::shared_ptr<ManagedHost> host);

  int32_t start(int32_t port, bool editMode, const std::string& authToken,
                const ServerCallbacks& callbacks) override;

  void stop() override;

  void broadcast(uint8_t type, std::span<const uint8_t> payload) const override;

  void sendToMany(std::span<const int32_t> connIds, uint8_t type, std::span<const uint8_t> payload) const override;

  [[nodiscard]] int32_t connectionCount() const override;

private:
  std::shared_ptr<ManagedHost> m_host;

  // Resolved [UnmanagedCallersOnly] entrypoints in the C# transport assembly.
  void* m_startFn = nullptr;
  void* m_stopFn = nullptr;
  void* m_broadcastFn = nullptr;
  void* m_connectionCountFn = nullptr;
  void* m_sendToManyFn = nullptr;
};

}

#endif //MANAGEDSERVERTRANSPORT_H
