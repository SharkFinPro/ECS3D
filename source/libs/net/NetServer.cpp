#include "NetServer.h"
#include "TransportLog.h"
#include <ManagedHost.h>
#include <Log.h>
#include <LogEntry.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <span>
#include <string>
#include <utility>

namespace net {

namespace {
  // The C# transport assembly, published next to the executable by ecs3d_add_managed_assembly.
  const std::string kAssembly = "net/Transport/ECS3DNetTransport.dll";
  const std::string kType = "ECS3DNetTransport.Transport, ECS3DNetTransport";

  // Matches ECS3DNetTransport.ServerStartError (Transport.cs): what serverStart returns on failure.
  enum class ServerStartError : int32_t
  {
    addressInUse = -1,
    invalidPort = -2,
    failed = -3,
  };

  // Returns the bound port, or a ServerStartError.
  using ServerStartFn = int32_t(*)(int32_t, uint8_t, const char*);
  using ServerStopFn = void(*)();
  using ServerBroadcastFn = void(*)(uint8_t, const uint8_t*, int32_t);
  // connIds/connIdCount name the specific connections to send to (editor connections only, never every
  // connection like ServerBroadcastFn) - one call for the whole fan-out, so the transport can apply one
  // shared deadline across it the same way it already does for a broadcast.
  using ServerSendToManyFn = void(*)(const int32_t*, int32_t, uint8_t, const uint8_t*, int32_t);
  using ServerConnectionCountFn = int32_t(*)();
  using SetCallbackFn = void(*)(void*);

  // One authoritative server per process; the C# socket thread routes inbound messages here. Atomic
  // because it is written on the tick thread (start/stop) and read on the socket threads with no other
  // synchronization between them - a plain pointer here would be a data race even though the callbacks
  // null-check it.
  std::atomic<NetServer*> g_activeServer{nullptr};
}

extern "C" void ecs3dNetServerReceive(const int32_t connId, const uint8_t type, const uint8_t* data, const int32_t len)
{
  if (auto* server = g_activeServer.load(std::memory_order_acquire))
  {
    server->enqueue(connId, type, data, len);
  }
}

extern "C" void ecs3dNetServerDisconnect(const int32_t connId)
{
  if (auto* server = g_activeServer.load(std::memory_order_acquire))
  {
    server->enqueueDisconnect(connId);
  }
}

extern "C" void ecs3dNetServerAuthorized(const int32_t connId, const uint8_t role)
{
  if (auto* server = g_activeServer.load(std::memory_order_acquire))
  {
    server->authorize(connId, role);
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
  m_sendToManyFn = m_host->getDelegate(kAssembly, kType, "serverSendToMany");
  m_connectionCountFn = m_host->getDelegate(kAssembly, kType, "serverConnectionCount");
  m_setCallbackFn = m_host->getDelegate(kAssembly, kType, "serverSetReceiveCallback");
  m_setDisconnectCallbackFn = m_host->getDelegate(kAssembly, kType, "serverSetDisconnectCallback");
  m_setAuthorizedCallbackFn = m_host->getDelegate(kAssembly, kType, "serverSetAuthorizedCallback");
  m_setLogCallbackFn = m_host->getDelegate(kAssembly, kType, "setLogCallback");

  g_activeServer.store(this, std::memory_order_release);
  reinterpret_cast<SetCallbackFn>(m_setCallbackFn)(reinterpret_cast<void*>(&ecs3dNetServerReceive));
  reinterpret_cast<SetCallbackFn>(m_setDisconnectCallbackFn)(reinterpret_cast<void*>(&ecs3dNetServerDisconnect));
  reinterpret_cast<SetCallbackFn>(m_setAuthorizedCallbackFn)(reinterpret_cast<void*>(&ecs3dNetServerAuthorized));
  reinterpret_cast<SetCallbackFn>(m_setLogCallbackFn)(reinterpret_cast<void*>(&transportLog));

  const auto result = reinterpret_cast<ServerStartFn>(m_startFn)(static_cast<int32_t>(port), m_editMode ? 1 : 0,
                                                                  authToken.c_str());
  if (result < 0)
  {
    g_activeServer.store(nullptr, std::memory_order_release);

    const auto portText = std::to_string(port);
    switch (static_cast<ServerStartError>(result))
    {
      case ServerStartError::addressInUse:
        throw std::runtime_error("Port " + portText + " is already in use by another process. "
                                 "Start the server with --port <other port>.");
      case ServerStartError::invalidPort:
        throw std::runtime_error("Port " + portText + " is out of range (0-65535).");
      default:
        throw std::runtime_error("Could not listen on port " + portText + ".");
    }
  }

  m_port = result;
  m_started = true;
}

int NetServer::port() const
{
  return m_port;
}

void NetServer::stop()
{
  if (!m_started)
  {
    return;
  }

  // The managed stop joins the accept thread and every per-connection receive thread before returning
  // (TcpBackend/WebSocketBackend ServerStop), so no socket thread can still be inside enqueue/
  // enqueueDisconnect/authorize by the time the pointer below is cleared. Clearing it after, rather than
  // before, matters only for a stray callback that could otherwise race the object's destruction; the
  // join is what actually makes that impossible.
  reinterpret_cast<ServerStopFn>(m_stopFn)();

  g_activeServer.store(nullptr, std::memory_order_release);
  m_started = false;
  m_port = 0;
}

void NetServer::broadcast(const Message& message) const
{
  if (!m_started)
  {
    return;
  }

  // A size above INT32_MAX would narrow to a negative or truncated frame length on the wire; refuse it
  // here rather than hand the cast something it cannot represent. This is a void hot-path callback, so
  // there is no caller to throw to - log the refusal instead.
  if (!fitsInWireFrameLength(message.size()))
  {
    Log::error(LogCategory::net, "Refusing to broadcast a " + std::to_string(message.size())
      + " byte message; the limit is " + std::to_string(std::numeric_limits<int32_t>::max()) + ".");
    return;
  }

  // Snapshots on join, state deltas per tick. The C# transport sends to every connected client.
  reinterpret_cast<ServerBroadcastFn>(m_broadcastFn)(
    static_cast<uint8_t>(message.getType()),
    message.bytes().data(),
    static_cast<int32_t>(message.size())
  );
}

void NetServer::sendToEditors(const Message& message) const
{
  if (!m_started)
  {
    return;
  }

  // A size above INT32_MAX would narrow to a negative or truncated frame length on the wire; refuse it
  // here rather than hand the cast something it cannot represent, the same guard broadcast() applies.
  if (!fitsInWireFrameLength(message.size()))
  {
    Log::error(LogCategory::net, "Refusing to send a " + std::to_string(message.size())
      + " byte message; the limit is " + std::to_string(std::numeric_limits<int32_t>::max()) + ".");
    return;
  }

  // Copied out under the lock and the lock released before the transport is touched at all: this runs on
  // the authoritative tick thread, and sendToMany below can block on a stalled socket. Holding the lock
  // across that would let a stalled editor connection block authorize()/isEditor()/takeDisconnected() -
  // none of which have anything to do with sending - for as long as the transport's own send budget runs.
  const auto connIds = m_connections.editorIds();
  if (connIds.empty())
  {
    return;
  }

  // One call for the whole fan-out (rather than one per editor) so the transport can apply a single
  // shared time budget across every editor, the same way ServerBroadcast already does across every
  // connection - a per-connection call here would let K stalled editors cost this tick K times the
  // transport's per-send timeout instead of that timeout once.
  reinterpret_cast<ServerSendToManyFn>(m_sendToManyFn)(
    connIds.data(),
    static_cast<int32_t>(connIds.size()),
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

bool NetServer::poll(Message& message, int32_t& senderId)
{
  return m_inbox.pop(message, senderId);
}

void NetServer::enqueue(const int32_t connId, const uint8_t type, const uint8_t* data, const int32_t len)
{
  // An empty payload is legal (a join may carry no nonce, and ServerApp::handleJoin keys on that),
  // so a zero or negative length, or a null buffer, is an empty message rather than a range to walk.
  const auto payload = len > 0 && data != nullptr
    ? std::span<const uint8_t>(data, static_cast<std::size_t>(len))
    : std::span<const uint8_t>();

  Message message(static_cast<MessageType>(type), payload);

  m_inbox.push(std::move(message), connId);
}

void NetServer::enqueueDisconnect(const int32_t connId)
{
  m_connections.enqueueDisconnect(connId);
}

std::vector<int32_t> NetServer::takeDisconnected()
{
  // Called on the tick thread after that tick's inbox drain, so any message from a now-dropped
  // connection has already been handled with its authorization intact.
  return m_connections.takeDisconnected();
}

void NetServer::authorize(const int32_t connId, const uint8_t role)
{
  m_connections.authorize(connId, role);
}

bool NetServer::isEditor(const int32_t connectionId) const
{
  return m_connections.isEditor(connectionId);
}

}
