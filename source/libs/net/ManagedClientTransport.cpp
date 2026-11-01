#include "ManagedClientTransport.h"
#include "TransportLog.h"
#include <ManagedHost.h>
#include <utility>

namespace net {

namespace {
  // The C# transport assembly, published next to the executable by ecs3d_add_managed_assembly.
  const std::string kAssembly = "net/Transport/ECS3DNetTransport.dll";
  const std::string kType = "ECS3DNetTransport.Transport, ECS3DNetTransport";

  using ClientConnectFn = uint8_t(*)(const char*, int32_t, uint8_t, const char*);
  using ClientDisconnectFn = void(*)();
  using ClientSendFn = void(*)(uint8_t, const uint8_t*, int32_t);
  using SetCallbackFn = void(*)(void*);
}

ManagedClientTransport::ManagedClientTransport(std::shared_ptr<ManagedHost> host)
  : m_host(std::move(host))
{}

bool ManagedClientTransport::connect(const std::string& host, const int32_t port, const Role role,
                                     const std::string& authToken, const ClientCallbacks& callbacks)
{
  m_connectFn = m_host->getDelegate(kAssembly, kType, "clientConnect");
  m_disconnectFn = m_host->getDelegate(kAssembly, kType, "clientDisconnect");
  m_sendFn = m_host->getDelegate(kAssembly, kType, "clientSend");
  void* setCallbackFn = m_host->getDelegate(kAssembly, kType, "clientSetReceiveCallback");
  void* setDisconnectCallbackFn = m_host->getDelegate(kAssembly, kType, "clientSetDisconnectCallback");
  void* setLogCallbackFn = m_host->getDelegate(kAssembly, kType, "setLogCallback");

  reinterpret_cast<SetCallbackFn>(setCallbackFn)(reinterpret_cast<void*>(callbacks.receive));
  reinterpret_cast<SetCallbackFn>(setDisconnectCallbackFn)(reinterpret_cast<void*>(callbacks.disconnect));
  reinterpret_cast<SetCallbackFn>(setLogCallbackFn)(reinterpret_cast<void*>(&transportLog));

  // role + authToken are sent at the handshake; the server grants Role::editor only if its edit-mode
  // launch gate is enabled and the token authorizes it. Same wire format for singleplayer (loopback),
  // local-MP, and remote.
  const uint8_t ok = reinterpret_cast<ClientConnectFn>(m_connectFn)(
    host.c_str(),
    port,
    static_cast<uint8_t>(role),
    authToken.c_str()
  );

  return ok != 0;
}

void ManagedClientTransport::disconnect()
{
  reinterpret_cast<ClientDisconnectFn>(m_disconnectFn)();
}

void ManagedClientTransport::send(const uint8_t type, const std::span<const uint8_t> payload) const
{
  reinterpret_cast<ClientSendFn>(m_sendFn)(type, payload.data(), static_cast<int32_t>(payload.size()));
}

}
