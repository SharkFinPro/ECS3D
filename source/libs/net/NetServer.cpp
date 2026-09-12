#include "NetServer.h"
#include <ManagedHost.h>
#include <array>
#include <iostream>
#include <limits>
#include <utility>

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

extern "C" void ecs3dNetServerReceive(const int32_t connId, const uint8_t type, const uint8_t* data, const int32_t len)
{
  if (g_activeServer)
  {
    g_activeServer->enqueue(connId, type, data, len);
  }
}

extern "C" void ecs3dNetServerDisconnect(const int32_t connId)
{
  if (g_activeServer)
  {
    g_activeServer->enqueueDisconnect(connId);
  }
}

extern "C" void ecs3dNetServerAuthorized(const int32_t connId, const uint8_t role)
{
  if (g_activeServer)
  {
    g_activeServer->authorize(connId, role);
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
  m_setDisconnectCallbackFn = m_host->getDelegate(kAssembly, kType, "serverSetDisconnectCallback");
  m_setAuthorizedCallbackFn = m_host->getDelegate(kAssembly, kType, "serverSetAuthorizedCallback");

  g_activeServer = this;
  reinterpret_cast<SetCallbackFn>(m_setCallbackFn)(reinterpret_cast<void*>(&ecs3dNetServerReceive));
  reinterpret_cast<SetCallbackFn>(m_setDisconnectCallbackFn)(reinterpret_cast<void*>(&ecs3dNetServerDisconnect));
  reinterpret_cast<SetCallbackFn>(m_setAuthorizedCallbackFn)(reinterpret_cast<void*>(&ecs3dNetServerAuthorized));

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

  // A size above INT32_MAX would narrow to a negative or truncated frame length on the wire; refuse it
  // here rather than hand the cast something it cannot represent. This is a void hot-path callback, so
  // there is no caller to throw to - log the refusal instead.
  if (!fitsInWireFrameLength(message.size()))
  {
    std::cerr << "[NetServer] Refusing to broadcast a " << message.size() << " byte message; the limit is "
              << std::numeric_limits<int32_t>::max() << "." << std::endl;
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

bool NetServer::poll(Message& message, int32_t& senderId)
{
  return m_inbox.pop(message, senderId);
}

void NetServer::enqueue(const int32_t connId, const uint8_t type, const uint8_t* data, const int32_t len)
{
  Message message(static_cast<MessageType>(type));
  for (const std::vector<uint8_t> chunks(data, data + len); const auto& chunk : chunks)
  {
    message.write(chunk);
  }

  m_inbox.push(std::move(message), connId);
}

void NetServer::enqueueDisconnect(const int32_t connId)
{
  // isEditor must not be revoked here: this runs on the socket thread, and a message this same
  // connection sent moments before disconnecting may still be sitting in the inbox, undrained until the
  // tick thread's next poll loop. Erasing now would make that message read back as unauthorized - an
  // editor that edits and then closes (an ordinary flow) would have its last edit refused and logged.
  std::lock_guard lock(m_disconnectMutex);
  m_disconnected.push_back(connId);
}

std::vector<int32_t> NetServer::takeDisconnected()
{
  std::vector<int32_t> disconnected;
  {
    std::lock_guard lock(m_disconnectMutex);
    disconnected = std::exchange(m_disconnected, {});
  }

  // Called on the tick thread after that tick's inbox drain, so any message from a now-dropped
  // connection has already been handled with its authorization intact. Connection ids are never
  // reused (both transport backends assign them via Interlocked.Increment), so a deferred erase here
  // cannot let a new connection inherit a stale editor entry.
  {
    std::lock_guard lock(m_editorMutex);
    for (const auto connId : disconnected)
    {
      m_editorConnections.erase(connId);
    }
  }

  return disconnected;
}

void NetServer::authorize(const int32_t connId, const uint8_t role)
{
  if (static_cast<Role>(role) != Role::editor)
  {
    return;
  }

  std::lock_guard lock(m_editorMutex);
  m_editorConnections.insert(connId);
}

bool NetServer::isEditor(const int32_t connectionId) const
{
  std::lock_guard lock(m_editorMutex);
  return m_editorConnections.contains(connectionId);
}

}
