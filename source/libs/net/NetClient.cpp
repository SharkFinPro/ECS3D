#include "NetClient.h"
#include <ManagedHost.h>
#include <array>

namespace net {

namespace {
  // The C# transport assembly, published next to the executable by ecs3d_add_managed_assembly.
  const std::string kAssembly = "net/Transport/ECS3DNetTransport.dll";
  const std::string kType = "ECS3DNetTransport.Transport, ECS3DNetTransport";

  using ClientConnectFn = uint8_t(*)(const char*, int32_t, uint8_t, const char*);
  using ClientDisconnectFn = void(*)();
  using ClientSendFn = void(*)(uint8_t, const uint8_t*, int32_t);
  using SetCallbackFn = void(*)(void*);

  // One connection per process; the C# socket thread routes inbound messages here.
  NetClient* g_activeClient = nullptr;
}

extern "C" void ecs3dNetClientReceive(const uint8_t type, const uint8_t* data, const int32_t len)
{
  if (g_activeClient)
  {
    g_activeClient->enqueue(type, data, len);
  }
}

NetClient::NetClient(std::shared_ptr<ManagedHost> host)
  : m_host(std::move(host))
{}

void NetClient::connect(const std::string& host, const int port, const Role role, const std::string& authToken)
{
  if (m_connected)
  {
    return;
  }

  m_connectFn = m_host->getDelegate(kAssembly, kType, "clientConnect");
  m_disconnectFn = m_host->getDelegate(kAssembly, kType, "clientDisconnect");
  m_sendFn = m_host->getDelegate(kAssembly, kType, "clientSend");
  m_setCallbackFn = m_host->getDelegate(kAssembly, kType, "clientSetReceiveCallback");

  g_activeClient = this;
  reinterpret_cast<SetCallbackFn>(m_setCallbackFn)(reinterpret_cast<void*>(&ecs3dNetClientReceive));

  // role + authToken are sent at the handshake; the server grants Role::editor only if its edit-mode
  // launch gate is enabled and the token authorizes it. Same wire format for singleplayer (loopback),
  // local-MP, and remote.
  const uint8_t ok = reinterpret_cast<ClientConnectFn>(m_connectFn)(
    host.c_str(),
    static_cast<int32_t>(port),
    static_cast<uint8_t>(role),
    authToken.c_str()
  );

  m_connected = ok != 0;
}

void NetClient::disconnect()
{
  if (!m_connected)
  {
    return;
  }

  reinterpret_cast<ClientDisconnectFn>(m_disconnectFn)();

  g_activeClient = nullptr;
  m_connected = false;
}

void NetClient::send(const Message& message) const
{
  if (!m_connected)
  {
    return;
  }

  reinterpret_cast<ClientSendFn>(m_sendFn)(
    static_cast<uint8_t>(message.getType()),
    message.bytes().data(),
    static_cast<int32_t>(message.size())
  );
}

bool NetClient::poll(Message& message)
{
  // The client has a single peer (the server), so the sender id is meaningless here — discard it.
  int32_t senderId = 0;
  return m_inbox.pop(message, senderId);
}

void NetClient::enqueue(const uint8_t type, const uint8_t* data, const int32_t len)
{
  Message message(static_cast<MessageType>(type));
  for (const std::vector<uint8_t> chunks(data, data + len); const auto& chunk : chunks)
  {
    message.write(chunk);
  }

  m_inbox.push(std::move(message));
}

}
