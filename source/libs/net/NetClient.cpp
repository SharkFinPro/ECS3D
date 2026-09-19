#include "NetClient.h"
#include "TransportLog.h"
#include <ManagedHost.h>
#include <Log.h>
#include <LogEntry.h>
#include <array>
#include <cstddef>
#include <limits>
#include <span>

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

extern "C" void ecs3dNetClientDisconnect()
{
  if (g_activeClient)
  {
    g_activeClient->enqueueDisconnect();
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

  // A fresh connection attempt: any earlier lifecycle's requested-disconnect no longer applies.
  m_disconnectRequested = false;

  m_connectFn = m_host->getDelegate(kAssembly, kType, "clientConnect");
  m_disconnectFn = m_host->getDelegate(kAssembly, kType, "clientDisconnect");
  m_sendFn = m_host->getDelegate(kAssembly, kType, "clientSend");
  m_setCallbackFn = m_host->getDelegate(kAssembly, kType, "clientSetReceiveCallback");
  m_setDisconnectCallbackFn = m_host->getDelegate(kAssembly, kType, "clientSetDisconnectCallback");
  m_setLogCallbackFn = m_host->getDelegate(kAssembly, kType, "setLogCallback");

  g_activeClient = this;
  reinterpret_cast<SetCallbackFn>(m_setCallbackFn)(reinterpret_cast<void*>(&ecs3dNetClientReceive));
  reinterpret_cast<SetCallbackFn>(m_setDisconnectCallbackFn)(reinterpret_cast<void*>(&ecs3dNetClientDisconnect));
  reinterpret_cast<SetCallbackFn>(m_setLogCallbackFn)(reinterpret_cast<void*>(&transportLog));

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

  // Set before the managed call: closing the socket below wakes the receive thread, which may reach
  // enqueueDisconnect before this function returns.
  m_disconnectRequested = true;

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

  // A size above INT32_MAX would narrow to a negative or truncated frame length on the wire; refuse it
  // here rather than hand the cast something it cannot represent. This is a void hot-path callback, so
  // there is no caller to throw to - log the refusal instead.
  if (!fitsInWireFrameLength(message.size()))
  {
    Log::error(LogCategory::net, "Refusing to send a " + std::to_string(message.size())
      + " byte message; the limit is " + std::to_string(std::numeric_limits<int32_t>::max()) + ".");
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
  // The client has a single peer (the server), so the sender id is meaningless here - discard it.
  int32_t senderId = 0;
  return m_inbox.pop(message, senderId);
}

void NetClient::enqueue(const uint8_t type, const uint8_t* data, const int32_t len)
{
  // An empty payload is legal, so a zero or negative length, or a null buffer, is an empty message
  // rather than a range to walk.
  const auto payload = len > 0 && data != nullptr
    ? std::span<const uint8_t>(data, static_cast<std::size_t>(len))
    : std::span<const uint8_t>();

  Message message(static_cast<MessageType>(type), payload);

  m_inbox.push(std::move(message));
}

void NetClient::enqueueDisconnect()
{
  // A disconnect this side asked for is not a lost connection - disconnect() already set m_connected
  // false and has no notice to show.
  if (m_disconnectRequested)
  {
    return;
  }

  m_connected = false;
  m_connectionLost = true;
}

bool NetClient::takeConnectionLost()
{
  return m_connectionLost.exchange(false);
}

}
