#include "NetServer.h"
#include <ManagedHost.h>
#include <array>

namespace net {

namespace {
  // The C# transport assembly, published next to the executable by ecs3d_add_managed_assembly.
  const std::string kAssembly = "net/Transport/ECS3DNetTransport.dll";
  const std::string kType = "ECS3DNetTransport.Transport, ECS3DNetTransport";

  using ServerStartFn = void(*)(int32_t, uint8_t, const char*);
  using ServerStopFn = void(*)();
  using ServerBroadcastFn = void(*)(uint8_t, const uint8_t*, int32_t);
  using ServerConnectionCountFn = int32_t(*)();
  using SetCallbackFn = void(*)(void*);

  // One authoritative server per process; the C# socket thread routes inbound messages here.
  NetServer* g_activeServer = nullptr;
}

extern "C" void ecs3dNetServerReceive(const uint8_t type, const uint8_t* data, const int32_t len)
{
  if (g_activeServer)
  {
    g_activeServer->enqueue(type, data, len);
  }
}

NetServer::NetServer(std::shared_ptr<ManagedHost> host)
  : m_host(std::move(host))
{}

void NetServer::start(const int port, const bool editMode, const std::string& authToken)
{
  if (m_started)
  {
    return;
  }

  // The editMode flag is the launch-capability gate: only when set may a connection be granted
  // Role::editor at the handshake, and only if it presents authToken (when one is configured).
  m_editMode = editMode;

  m_startFn = m_host->getDelegate(kAssembly, kType, "serverStart");
  m_stopFn = m_host->getDelegate(kAssembly, kType, "serverStop");
  m_broadcastFn = m_host->getDelegate(kAssembly, kType, "serverBroadcast");
  m_connectionCountFn = m_host->getDelegate(kAssembly, kType, "serverConnectionCount");
  m_setCallbackFn = m_host->getDelegate(kAssembly, kType, "serverSetReceiveCallback");

  g_activeServer = this;
  reinterpret_cast<SetCallbackFn>(m_setCallbackFn)(reinterpret_cast<void*>(&ecs3dNetServerReceive));

  reinterpret_cast<ServerStartFn>(m_startFn)(static_cast<int32_t>(port), m_editMode ? 1 : 0, authToken.c_str());
  m_started = true;
}

void NetServer::stop()
{
  if (!m_started)
  {
    return;
  }

  reinterpret_cast<ServerStopFn>(m_stopFn)();

  g_activeServer = nullptr;
  m_started = false;
}

void NetServer::broadcast(const Message& message) const
{
  if (!m_started)
  {
    return;
  }

  // Snapshots on join, state deltas per tick. The C# transport sends to every connected client.
  reinterpret_cast<ServerBroadcastFn>(m_broadcastFn)(
    static_cast<uint8_t>(message.getType()),
    message.bytes().data(),
    static_cast<int32_t>(message.size())
  );
}

int NetServer::connectionCount() const
{
  if (!m_started)
  {
    return 0;
  }

  return reinterpret_cast<ServerConnectionCountFn>(m_connectionCountFn)();
}

bool NetServer::poll(Message& message)
{
  return m_inbox.pop(message);
}

void NetServer::enqueue(const uint8_t type, const uint8_t* data, const int32_t len)
{
  Message message(static_cast<MessageType>(type));
  for (const std::vector<uint8_t> chunks(data, data + len); const auto& chunk : chunks)
  {
    message.write(chunk);
  }

  // Message message {
  //   .type = static_cast<MessageType>(type),
  //   .payload = std::vector<uint8_t>(data, data + len)
  // };

  m_inbox.push(std::move(message));
}

}
