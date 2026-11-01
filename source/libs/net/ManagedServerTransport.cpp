#include "ManagedServerTransport.h"
#include "TransportLog.h"
#include <ManagedHost.h>
#include <utility>

namespace net {

namespace {
  // The C# transport assembly, published next to the executable by ecs3d_add_managed_assembly.
  const std::string kAssembly = "net/Transport/ECS3DNetTransport.dll";
  const std::string kType = "ECS3DNetTransport.Transport, ECS3DNetTransport";

  // Returns the bound port, or a ServerStartError.
  using ServerStartFn = int32_t(*)(int32_t, uint8_t, const char*);
  using ServerStopFn = void(*)();
  using ServerBroadcastFn = void(*)(uint8_t, const uint8_t*, int32_t);
  // connIds/connIdCount name the specific connections to send to (never every
  // connection like ServerBroadcastFn) - one call for the whole fan-out. The transport queues the message
  // per connection, so the call never blocks on a slow peer.
  using ServerSendToManyFn = void(*)(const int32_t*, int32_t, uint8_t, const uint8_t*, int32_t);
  using ServerConnectionCountFn = int32_t(*)();
  using SetCallbackFn = void(*)(void*);
}

ManagedServerTransport::ManagedServerTransport(std::shared_ptr<ManagedHost> host)
  : m_host(std::move(host))
{}

int32_t ManagedServerTransport::start(const int32_t port, const bool editMode, const std::string& authToken,
                                      const ServerCallbacks& callbacks)
{
  m_startFn = m_host->getDelegate(kAssembly, kType, "serverStart");
  m_stopFn = m_host->getDelegate(kAssembly, kType, "serverStop");
  m_broadcastFn = m_host->getDelegate(kAssembly, kType, "serverBroadcast");
  m_sendToManyFn = m_host->getDelegate(kAssembly, kType, "serverSendToMany");
  m_connectionCountFn = m_host->getDelegate(kAssembly, kType, "serverConnectionCount");
  void* setCallbackFn = m_host->getDelegate(kAssembly, kType, "serverSetReceiveCallback");
  void* setDisconnectCallbackFn = m_host->getDelegate(kAssembly, kType, "serverSetDisconnectCallback");
  void* setAuthorizedCallbackFn = m_host->getDelegate(kAssembly, kType, "serverSetAuthorizedCallback");
  void* setLogCallbackFn = m_host->getDelegate(kAssembly, kType, "setLogCallback");

  reinterpret_cast<SetCallbackFn>(setCallbackFn)(reinterpret_cast<void*>(callbacks.receive));
  reinterpret_cast<SetCallbackFn>(setDisconnectCallbackFn)(reinterpret_cast<void*>(callbacks.disconnect));
  reinterpret_cast<SetCallbackFn>(setAuthorizedCallbackFn)(reinterpret_cast<void*>(callbacks.authorized));
  reinterpret_cast<SetCallbackFn>(setLogCallbackFn)(reinterpret_cast<void*>(&transportLog));

  return reinterpret_cast<ServerStartFn>(m_startFn)(port, editMode ? 1 : 0, authToken.c_str());
}

void ManagedServerTransport::stop()
{
  reinterpret_cast<ServerStopFn>(m_stopFn)();
}

void ManagedServerTransport::broadcast(const uint8_t type, const std::span<const uint8_t> payload) const
{
  // The C# transport sends to every connected client.
  reinterpret_cast<ServerBroadcastFn>(m_broadcastFn)(type, payload.data(), static_cast<int32_t>(payload.size()));
}

void ManagedServerTransport::sendToMany(const std::span<const int32_t> connIds, const uint8_t type,
                                        const std::span<const uint8_t> payload) const
{
  // One call for the whole fan-out (rather than one per connection) so the transport snapshots its
  // connection list once; the transport queues per connection and never blocks on a slow peer.
  reinterpret_cast<ServerSendToManyFn>(m_sendToManyFn)(
    connIds.data(),
    static_cast<int32_t>(connIds.size()),
    type,
    payload.data(),
    static_cast<int32_t>(payload.size())
  );
}

int32_t ManagedServerTransport::connectionCount() const
{
  return reinterpret_cast<ServerConnectionCountFn>(m_connectionCountFn)();
}

}
